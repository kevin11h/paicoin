// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "validation.h"
#include "miner.h"
#include "policy/policy.h"
#include "pubkey.h"
#include "script/standard.h"
#include "txmempool.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"

#include "test/test_paicoin.h"

#include <memory>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(miner_tests, TestingSetup)

static CFeeRate blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);

static BlockAssembler AssemblerForTest(const CChainParams& params) {
    BlockAssembler::Options options;

    options.nBlockMaxWeight = MAX_BLOCK_WEIGHT;
    options.blockMinFeeRate = blockMinFeeRate;
    return BlockAssembler(params, options);
}

static
struct {
    unsigned char extranonce;
    unsigned int nonce;
} blockinfo[] = {
    /* PAICOIN Note:
     * The extranonce-nonce pairs bellow must be changed, but only when the actual production genesis block is ready:
     * 1. Generate the 110 block that are to be added after the genesis block, similar to the Bitcoin's test blocks in miner-test-blocks.txt
     *    Note the usage of the median time for nTime (starting with the genesis block time), the chain height in the scriptSig, the extranonce, etc.
     *    Also, make use of the code in CreateNewBlock_validity below.
     * 2. Update the extranonce-nonce pairs below with the newly generated values.
     * 3. In CreateNewBlock_validity, remove the comment and the return at the beginning of the function.
     * 4. Run make check
     */
    {4, 0xa4a3e223}, {2, 0x15c32f9e}, {1, 0x0375b547}, {1, 0x7004a8a5},
    {2, 0xce440296}, {2, 0x52cfe198}, {1, 0x77a72cd0}, {2, 0xbb5d6f84},
    {2, 0x83f30c2c}, {1, 0x48a73d5b}, {1, 0xef7dcd01}, {2, 0x6809c6c4},
    {2, 0x0883ab3c}, {1, 0x087bbbe2}, {2, 0x2104a814}, {2, 0xdffb6daa},
    {1, 0xee8a0a08}, {2, 0xba4237c1}, {1, 0xa70349dc}, {1, 0x344722bb},
    {3, 0xd6294733}, {2, 0xec9f5c94}, {2, 0xca2fbc28}, {1, 0x6ba4f406},
    {2, 0x015d4532}, {1, 0x6e119b7c}, {2, 0x43e8f314}, {2, 0x27962f38},
    {2, 0xb571b51b}, {2, 0xb36bee23}, {2, 0xd17924a8}, {2, 0x6bc212d9},
    {1, 0x630d4948}, {2, 0x9a4c4ebb}, {2, 0x554be537}, {1, 0xd63ddfc7},
    {2, 0xa10acc11}, {1, 0x759a8363}, {2, 0xfb73090d}, {1, 0xe82c6a34},
    {1, 0xe33e92d7}, {3, 0x658ef5cb}, {2, 0xba32ff22}, {5, 0x0227a10c},
    {1, 0xa9a70155}, {5, 0xd096d809}, {1, 0x37176174}, {1, 0x830b8d0f},
    {1, 0xc6e3910e}, {2, 0x823f3ca8}, {1, 0x99850849}, {1, 0x7521fb81},
    {1, 0xaacaabab}, {1, 0xd645a2eb}, {5, 0x7aea1781}, {5, 0x9d6e4b78},
    {1, 0x4ce90fd8}, {1, 0xabdc832d}, {6, 0x4a34f32a}, {2, 0xf2524c1c},
    {2, 0x1bbeb08a}, {1, 0xad47f480}, {1, 0x9f026aeb}, {1, 0x15a95049},
    {2, 0xd1cb95b2}, {2, 0xf84bbda5}, {1, 0x0fa62cd1}, {1, 0xe05f9169},
    {1, 0x78d194a9}, {5, 0x3e38147b}, {5, 0x737ba0d4}, {1, 0x63378e10},
    {1, 0x6d5f91cf}, {2, 0x88612eb8}, {2, 0xe9639484}, {1, 0xb7fabc9d},
    {2, 0x19b01592}, {1, 0x5a90dd31}, {2, 0x5bd7e028}, {2, 0x94d00323},
    {1, 0xa9b9c01a}, {1, 0x3a40de61}, {1, 0x56e7eec7}, {5, 0x859f7ef6},
    {1, 0xfd8e5630}, {1, 0x2b0c9f7f}, {1, 0xba700e26}, {1, 0x7170a408},
    {1, 0x70de86a8}, {1, 0x74d64cd5}, {1, 0x49e738a1}, {2, 0x6910b602},
    {0, 0x643c565f}, {1, 0x54264b3f}, {2, 0x97ea6396}, {2, 0x55174459},
    {2, 0x03e8779a}, {1, 0x98f34d8f}, {1, 0xc07b2b07}, {1, 0xdfe29668},
    {1, 0x3141c7c1}, {1, 0xb3b595f4}, {1, 0x735abf08}, {5, 0x623bfbce},
    {2, 0xd351e722}, {1, 0xf4ca48c9}, {1, 0x5b19c670}, {1, 0xa164bf0e},
    {2, 0xbbbeb305}, {2, 0xfe1c810a},
};

CBlockIndex CreateBlockIndex(int nHeight)
{
    CBlockIndex index;
    index.nHeight = nHeight;
    index.pprev = chainActive.Tip();
    return index;
}

bool TestSequenceLocks(const CTransaction &tx, int flags)
{
    LOCK(mempool.cs);
    return CheckSequenceLocks(tx, flags);
}

// Test suite for ancestor feerate transaction selection.
// Implemented as an additional function, rather than a separate test case,
// to allow reusing the blockchain created in CreateNewBlock_validity.
void TestPackageSelection(const CChainParams& chainparams, CScript scriptPubKey, std::vector<CTransactionRef>& txFirst)
{
    // Test the ancestor feerate transaction selection.
    TestMemPoolEntryHelper entry;

    // Test that a medium fee transaction will be selected after a higher fee
    // rate package with a low fee rate parent.
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 5000000000LL - 1000;
    // This tx has a low fee: 1000 satoshis
    uint256 hashParentTx = tx.GetHash(); // save this txid for later use
    mempool.addUnchecked(hashParentTx, entry.Fee(1000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a medium fee: 10000 satoshis
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vout[0].nValue = 5000000000LL - 10000;
    uint256 hashMediumFeeTx = tx.GetHash();
    mempool.addUnchecked(hashMediumFeeTx, entry.Fee(10000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a high fee, but depends on the first transaction
    tx.vin[0].prevout.hash = hashParentTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000; // 50k satoshi fee
    uint256 hashHighFeeTx = tx.GetHash();
    mempool.addUnchecked(hashHighFeeTx, entry.Fee(50000).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));

    std::unique_ptr<CBlockTemplate> pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate->block.vtx[1]->GetHash() == hashParentTx);
    BOOST_CHECK(pblocktemplate->block.vtx[2]->GetHash() == hashHighFeeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[3]->GetHash() == hashMediumFeeTx);

    // Test that a package below the block min tx fee doesn't get included
    tx.vin[0].prevout.hash = hashHighFeeTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000; // 0 fee
    uint256 hashFreeTx = tx.GetHash();
    mempool.addUnchecked(hashFreeTx, entry.Fee(0).FromTx(tx));
    size_t freeTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

    // Calculate a fee on child transaction that will put the package just
    // below the block min tx fee (assuming 1 child tx of the same size).
    CAmount feeToUse = blockMinFeeRate.GetFee(2*freeTxSize) - 1;

    tx.vin[0].prevout.hash = hashFreeTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000 - feeToUse;
    uint256 hashLowFeeTx = tx.GetHash();
    mempool.addUnchecked(hashLowFeeTx, entry.Fee(feeToUse).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    // Verify that the free tx and the low fee tx didn't get selected
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx);
    }

    // Test that packages above the min relay fee do get included, even if one
    // of the transactions is below the min relay fee
    // Remove the low fee transaction and replace with a higher fee transaction
    mempool.removeRecursive(tx);
    tx.vout[0].nValue -= 2; // Now we should be just over the min relay fee
    hashLowFeeTx = tx.GetHash();
    mempool.addUnchecked(hashLowFeeTx, entry.Fee(feeToUse+2).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate->block.vtx[4]->GetHash() == hashFreeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[5]->GetHash() == hashLowFeeTx);

    // Test that transaction selection properly updates ancestor fee
    // calculations as ancestor transactions get included in a block.
    // Add a 0-fee transaction that has 2 outputs.
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vout.resize(2);
    tx.vout[0].nValue = 5000000000LL - 100000000;
    tx.vout[1].nValue = 100000000; // 1PAI output
    uint256 hashFreeTx2 = tx.GetHash();
    mempool.addUnchecked(hashFreeTx2, entry.Fee(0).SpendsCoinbase(true).FromTx(tx));

    // This tx can't be mined by itself
    tx.vin[0].prevout.hash = hashFreeTx2;
    tx.vout.resize(1);
    feeToUse = blockMinFeeRate.GetFee(freeTxSize);
    tx.vout[0].nValue = 5000000000LL - 100000000 - feeToUse;
    uint256 hashLowFeeTx2 = tx.GetHash();
    mempool.addUnchecked(hashLowFeeTx2, entry.Fee(feeToUse).SpendsCoinbase(false).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);

    // Verify that this tx isn't selected.
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashFreeTx2);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHash() != hashLowFeeTx2);
    }

    // This tx will be mineable, and should cause hashLowFeeTx2 to be selected
    // as well.
    tx.vin[0].prevout.n = 1;
    tx.vout[0].nValue = 100000000 - 10000; // 10k satoshi fee
    mempool.addUnchecked(tx.GetHash(), entry.Fee(10000).FromTx(tx));
    pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate->block.vtx[8]->GetHash() == hashLowFeeTx2);
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)
{
    // PAICOIN Note: Activate this test by removing the following line once the test blocks are generated
    return;

    // Note that by default, these tests run with size accounting enabled.
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    const CChainParams& chainparams = *chainParams;
    // PAICOIN Note: No need to update this address, it's just for testing purposes
    CScript scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    CMutableTransaction tx,tx2;
    CScript script;
    uint256 hash;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.nHeight = 11;

    LOCK(cs_main);
    LOCK(::mempool.cs);
    fCheckpointsEnabled = false;

    // Simple block creation, nothing special yet:
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // We can't make transactions until we have inputs
    // Therefore, load 100 blocks :)
    int baseheight = 0;
    std::vector<CTransactionRef> txFirst;
    for (unsigned int i = 0; i < sizeof(blockinfo)/sizeof(*blockinfo); ++i)
    {
        CBlock *pblock = &pblocktemplate->block; // pointer for convenience
        pblock->nVersion = 1;
        pblock->nTime = chainActive.Tip()->GetMedianTimePast()+1;
        CMutableTransaction txCoinbase(*pblock->vtx[0]);
        txCoinbase.nVersion = 1;
        txCoinbase.vin[0].scriptSig = CScript();
        txCoinbase.vin[0].scriptSig.push_back(blockinfo[i].extranonce);
        txCoinbase.vin[0].scriptSig.push_back(chainActive.Height());
        txCoinbase.vout.resize(1); // Ignore the (optional) segwit commitment added by CreateNewBlock (as the hardcoded nonces don't account for this)
        txCoinbase.vout[0].scriptPubKey = CScript();
        pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
        if (txFirst.size() == 0)
            baseheight = chainActive.Height();
        if (txFirst.size() < 4)
            txFirst.push_back(pblock->vtx[0]);
        pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
        pblock->nNonce = blockinfo[i].nonce;
        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        BOOST_CHECK(ProcessNewBlock(chainparams, shared_pblock, true, nullptr));
        pblock->hashPrevBlock = pblock->GetHash();
    }

    // uncomment this block to generate the blockinfo
    // NOTE it takes an awful amount of time!
    // while(true) 
    // {
    //     CBlock *pblock = &pblocktemplate->block; // pointer for convenience
    //     pblock->nVersion = 1;
    //     pblock->nTime = chainActive.Tip()->GetMedianTimePast()+1;
    //     // CMutableTransaction txCoinbase(*pblock->vtx[0]);
    //     bool processBlock = false;
    //     unsigned int a = 17000000;
    //     while(!processBlock) {
    //         pblock->nNonce = a++;
    //         if(a % 1000000 == 0) {
    //             std::cout << "at count " << a << std::endl;
    //         }

    //     for(int j=0; j<7; j++) {
    //         // CBlock *pblock = &pblocktemplate->block; // pointer for convenience
    //         // pblock->nVersion = 1;
    //         // pblock->nTime = chainActive.Tip()->GetMedianTimePast()+1;
    //         CMutableTransaction txCoinbase(*pblock->vtx[0]);
    //         txCoinbase.nVersion = 1;
    //         txCoinbase.vin[0].scriptSig = CScript();
    //         txCoinbase.vin[0].scriptSig.push_back(j); //blockinfo[i].extranonce);
    //         txCoinbase.vin[0].scriptSig.push_back(chainActive.Height());
    //         txCoinbase.vout.resize(1); // Ignore the (optional) segwit commitment added by CreateNewBlock (as the hardcoded nonces don't account for this)
    //         txCoinbase.vout[0].scriptPubKey = CScript();
    //         pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    //         if (txFirst.size() == 0)
    //             baseheight = chainActive.Height();
    //         if (txFirst.size() < 4)
    //             txFirst.push_back(pblock->vtx[0]);
    //         pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    //         //pblock->nNonce = blockinfo[i].nonce;
    //         std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    //         processBlock = ProcessNewBlock(chainparams, shared_pblock, true, nullptr);
    //         //BOOST_CHECK(processBlock);
    //             if(processBlock) {
    //                 std::cout << "nounce is " << shared_pblock->nNonce << std::endl;
    //                 std::cout << "extra nounce is " << j << std::endl;
    //                 break;
    //             }
    //         }
    //     }
    //         //  std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    //         //  bool processBlock = ProcessNewBlock(chainparams, shared_pblock, true, nullptr);
    //         //   BOOST_CHECK(ProcessNewBlock(chainparams, shared_pblock, true, nullptr));
    //         pblock->hashPrevBlock = pblock->GetHash();
    // }

    // Just to make sure we can still make simple blocks
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // PAICOIN Note: If the initial block subsidy has been changed,
    // update the subsidy with the correct value
    const CAmount BLOCKSUBSIDY = chainParams->GetConsensus().nTotalBlockSubsidy * COIN;
    const CAmount LOWFEE = CENT;
    const CAmount HIGHFEE = COIN;
    const CAmount HIGHERFEE = 4*COIN;

    // block sigops > limit: 1000 CHECKMULTISIG + 1
    tx.vin.resize(1);
    // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP << OP_CHECKMULTISIG << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we don't set the # of sig ops in the CTxMemPoolEntry, template creation fails
        mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK_THROW(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error);
    mempool.clear();

    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we do set the # of sig ops in the CTxMemPoolEntry, template creation passes
        mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).SigOpsCost(80).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    mempool.clear();

    // block size > limit
    tx.vin[0].scriptSig = CScript();
    // 18 * (520char + DROP) + OP_1 = 9433 bytes
    std::vector<unsigned char> vchData(520);
    for (unsigned int i = 0; i < 18; ++i)
        tx.vin[0].scriptSig << vchData << OP_DROP;
    tx.vin[0].scriptSig << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 128; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    mempool.clear();

    // orphan in mempool, template creation fails
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).FromTx(tx));
    BOOST_CHECK_THROW(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error);
    mempool.clear();

    // child with higher feerate than parent
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin.resize(2);
    tx.vin[1].scriptSig = CScript() << OP_1;
    tx.vin[1].prevout.hash = txFirst[0]->GetHash();
    tx.vin[1].prevout.n = 0;
    tx.vout[0].nValue = tx.vout[0].nValue+BLOCKSUBSIDY-HIGHERFEE; //First txn output + fresh coinbase - new txn fee
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHERFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    mempool.clear();

    // coinbase in mempool, template creation fails
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_1;
    tx.vout[0].nValue = 0;
    hash = tx.GetHash();
    // give it a fee so it'll get mined
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    BOOST_CHECK_THROW(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error);
    mempool.clear();

    // invalid (pre-p2sh) txn in mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-LOWFEE;
    script = CScript() << OP_0;
    tx.vout[0].scriptPubKey = GetScriptForDestination(CScriptID(script));
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(script.begin(), script.end());
    tx.vout[0].nValue -= LOWFEE;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    BOOST_CHECK_THROW(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error);
    mempool.clear();

    // double spend txn pair in mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK_THROW(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error);
    mempool.clear();

    // subsidy changing
    int nHeight = chainActive.Height();
    // Create an actual 209999-long block chain (without valid blocks).
    while (chainActive.Tip()->nHeight < 209999) {
        CBlockIndex* prev = chainActive.Tip();
        CBlockIndex* next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        pcoinsTip->SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        chainActive.SetTip(next);
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    // Extend to a 210000-long block chain.
    while (chainActive.Tip()->nHeight < 210000) {
        CBlockIndex* prev = chainActive.Tip();
        CBlockIndex* next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        pcoinsTip->SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        chainActive.SetTip(next);
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    // Delete the dummy blocks again.
    while (chainActive.Tip()->nHeight > nHeight) {
        CBlockIndex* del = chainActive.Tip();
        chainActive.SetTip(del->pprev);
        pcoinsTip->SetBestBlock(del->pprev->GetBlockHash());
        delete del->phashBlock;
        delete del;
    }

    // non-final txs in mempool
    SetMockTime(chainActive.Tip()->GetMedianTimePast()+1);
    int flags = LOCKTIME_VERIFY_SEQUENCE|LOCKTIME_MEDIAN_TIME_PAST;
    // height map
    std::vector<int> prevheights;

    // relative height locked
    tx.nVersion = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    tx.vin[0].prevout.hash = txFirst[0]->GetHash(); // only 1 transaction
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].nSequence = chainActive.Tip()->nHeight + 1; // txFirst[0] is the 2nd block
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail
    BOOST_CHECK(SequenceLocks(tx, flags, &prevheights, CreateBlockIndex(chainActive.Tip()->nHeight + 2))); // Sequence locks pass on 2nd block

    // relative time locked
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | (((chainActive.Tip()->GetMedianTimePast()+1-chainActive[1]->GetMedianTimePast()) >> CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) + 1); // txFirst[1] is the 3rd block
    prevheights[0] = baseheight + 2;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    BOOST_CHECK(SequenceLocks(tx, flags, &prevheights, CreateBlockIndex(chainActive.Tip()->nHeight + 1))); // Sequence locks pass 512 seconds later
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime -= 512; //undo tricked MTP

    // absolute height locked
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL - 1;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = chainActive.Tip()->nHeight + 1;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(tx, flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(tx, chainActive.Tip()->nHeight + 2, chainActive.Tip()->GetMedianTimePast())); // Locktime passes on 2nd block

    // absolute time locked
    tx.vin[0].prevout.hash = txFirst[3]->GetHash();
    tx.nLockTime = chainActive.Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(tx, flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(tx, chainActive.Tip()->nHeight + 2, chainActive.Tip()->GetMedianTimePast() + 1)); // Locktime passes 1 second later

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout.hash = hash;
    prevheights[0] = chainActive.Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = 1;
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail

    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // None of the of the absolute height/time locked tx should have made
    // it into the template because we still check IsFinalTx in CreateNewBlock,
    // but relative locked txs will if inconsistently added to mempool.
    // For now these will still generate a valid template until BIP68 soft fork
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 3);
    // However if we advance height by 1 and time by 512, all of them should be mined
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    chainActive.Tip()->nHeight++;
    SetMockTime(chainActive.Tip()->GetMedianTimePast() + 1);

    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 5);

    chainActive.Tip()->nHeight--;
    SetMockTime(0);
    mempool.clear();

    TestPackageSelection(chainparams, scriptPubKey, txFirst);

    fCheckpointsEnabled = true;
}

struct TestChain100Setup_nokey : public TestChain100Setup
{
    TestChain100Setup_nokey() : TestChain100Setup(scriptPubKeyType::NoKey){
        ;
    }
};

BOOST_FIXTURE_TEST_CASE( CreateNewBlock_validity_REGTEST, TestChain100Setup_nokey)
{
    BOOST_CHECK_EQUAL(chainActive.Tip()->nHeight, 100);

    // Note that by default, these tests run with size accounting enabled.
    const CChainParams& chainparams = Params();
    // PAICOIN Note: No need to update this address, it's just for testing purposes
    CScript scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    CMutableTransaction tx;
    CScript script;
    uint256 hash;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.nHeight = 11;

    fCheckpointsEnabled = false;

    // add more blocks, to get to 110, above the COINBASE_MATURITY to be able to spend first coinbases
    // TODO there are two params atm COINBASE_MATURITY (set to 100 in consensus.h) and consensus.nCoinbaseMaturity (set in 
    // chainparams.cpp , network dependent); we need to remove one of them 
    for( int i = 0 ; i < 10 ; ++i) {
        CreateAndProcessBlock({},scriptPubKey);
    }
    BOOST_CHECK_EQUAL(chainActive.Tip()->nHeight, 110);

    // Simple block creation, nothing special yet:
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    int baseheight = 0;

    LOCK(cs_main);
    LOCK(::mempool.cs);

    // Just to make sure we can still make simple blocks
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // PAICOIN Note: If the initial block subsidy has been changed,
    // update the subsidy with the correct value
    const CAmount BLOCKSUBSIDY = chainparams.GetConsensus().nTotalBlockSubsidy * COIN;
    const CAmount LOWFEE = CENT;
    const CAmount HIGHFEE = COIN;
    const CAmount HIGHERFEE = 4*COIN;

    std::vector<CTransactionRef> txFirst;
    txFirst.resize(4);
    // copy first 4 coinbase txs
    for(int i = 0; i < 4; ++i) {
        txFirst[i] = MakeTransactionRef(std::move(coinbaseTxns[i]));
    }

    // block sigops > limit: 1000 CHECKMULTISIG + 1
    tx.vin.resize(1);
    // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP << OP_CHECKMULTISIG << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we don't set the # of sig ops in the CTxMemPoolEntry, template creation fails
        mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK_THROW(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error);
    mempool.clear();

    // block size > limit
    tx.vin[0].scriptSig = CScript();
    // 18 * (520char + DROP) + OP_1 = 9433 bytes
    std::vector<unsigned char> vchData(520);
    for (unsigned int i = 0; i < 18; ++i)
        tx.vin[0].scriptSig << vchData << OP_DROP;
    tx.vin[0].scriptSig << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 128; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    mempool.clear();

    // orphan in mempool, template creation fails
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).FromTx(tx));
    BOOST_CHECK_THROW(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error);
    mempool.clear();

    // child with higher feerate than parent
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin.resize(2);
    tx.vin[1].scriptSig = CScript() << OP_1;
    tx.vin[1].prevout.hash = txFirst[0]->GetHash();
    tx.vin[1].prevout.n = 0;
    tx.vout[0].nValue = tx.vout[0].nValue+BLOCKSUBSIDY-HIGHERFEE; //First txn output + fresh coinbase - new txn fee
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHERFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    mempool.clear();

    // coinbase in mempool, template creation fails
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_1;
    tx.vout[0].nValue = 0;
    hash = tx.GetHash();
    // give it a fee so it'll get mined
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    BOOST_CHECK_THROW(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error);
    mempool.clear();

    // invalid (pre-p2sh) txn in mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-LOWFEE;
    script = CScript() << OP_0;
    tx.vout[0].scriptPubKey = GetScriptForDestination(CScriptID(script));
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(script.begin(), script.end());
    tx.vout[0].nValue -= LOWFEE;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    BOOST_CHECK_THROW(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error);
    mempool.clear();

    // double spend txn pair in mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK_THROW(AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey), std::runtime_error);
    mempool.clear();

    // subsidy changing
    int nHeight = chainActive.Height();

    const auto& invalidBlocks = chainparams.GetConsensus().nStakeValidationHeight - 1;
    // we need to stop below nStakeValidationHeight, otherwise block validation will fail with bad-stakediff
    // Create an actual invalidBlocks-long block chain (without valid blocks).
    while (chainActive.Tip()->nHeight < invalidBlocks - 2) {
        CBlockIndex* prev = chainActive.Tip();
        CBlockIndex* next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        pcoinsTip->SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        chainActive.SetTip(next);
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    // Extend to the invalid block chain.
    while (chainActive.Tip()->nHeight < invalidBlocks - 1) {
        CBlockIndex* prev = chainActive.Tip();
        CBlockIndex* next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        pcoinsTip->SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        chainActive.SetTip(next);
    }
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    // Delete the dummy blocks again.
    while (chainActive.Tip()->nHeight > nHeight) {
        CBlockIndex* del = chainActive.Tip();
        chainActive.SetTip(del->pprev);
        pcoinsTip->SetBestBlock(del->pprev->GetBlockHash());
        delete del->phashBlock;
        delete del;
    }

    // non-final txs in mempool
    SetMockTime(chainActive.Tip()->GetMedianTimePast()+1);
    int flags = LOCKTIME_VERIFY_SEQUENCE|LOCKTIME_MEDIAN_TIME_PAST;
    // height map
    std::vector<int> prevheights;

    // relative height locked
    tx.nVersion = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    tx.vin[0].prevout.hash = txFirst[0]->GetHash(); // only 1 transaction
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].nSequence = chainActive.Tip()->nHeight + 1; // txFirst[0] is the 2nd block
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail
    BOOST_CHECK(SequenceLocks(tx, flags, &prevheights, CreateBlockIndex(chainActive.Tip()->nHeight + 2))); // Sequence locks pass on 2nd block

    // relative time locked
    tx.vin[0].prevout.hash =  txFirst[1]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | (((chainActive.Tip()->GetMedianTimePast()+1-chainActive[1]->GetMedianTimePast()) >> CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) + 1); // txFirst[1] is the 3rd block
    prevheights[0] = baseheight + 2;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    BOOST_CHECK(SequenceLocks(tx, flags, &prevheights, CreateBlockIndex(chainActive.Tip()->nHeight + 1))); // Sequence locks pass 512 seconds later
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime -= 512; //undo tricked MTP

    // absolute height locked
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL - 1;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = chainActive.Tip()->nHeight + 1;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(tx, flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(tx, chainActive.Tip()->nHeight + 2, chainActive.Tip()->GetMedianTimePast())); // Locktime passes on 2nd block

    // absolute time locked
    tx.vin[0].prevout.hash = txFirst[3]->GetHash();
    tx.nLockTime = chainActive.Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(tx, flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(tx, chainActive.Tip()->nHeight + 2, chainActive.Tip()->GetMedianTimePast() + 1)); // Locktime passes 1 second later

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout.hash = hash;
    prevheights[0] = chainActive.Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = 1;
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));

    // None of the of the absolute height/time locked tx should have made
    // it into the template because we still check IsFinalTx in CreateNewBlock,
    // but relative locked txs will if inconsistently added to mempool.
    // For now these will still generate a valid template until BIP68 soft fork
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 3);
    // However if we advance height by 1 and time by 512, all of them should be mined
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++)
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    chainActive.Tip()->nHeight++;
    SetMockTime(chainActive.Tip()->GetMedianTimePast() + 1);

    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKey));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 5);

    chainActive.Tip()->nHeight--;
    SetMockTime(0);
    mempool.clear();

    TestPackageSelection(chainparams, scriptPubKey, txFirst);

    fCheckpointsEnabled = true;
}

//TODO move these in a common place, now a verbatim copy from transaction_tests.cpp
CMutableTransaction CreateDummyBuyTicket( const CTransaction& prevTx, uint32_t nOut, const CAmount& ticketPrice, const CScript& stakeScript
                                        , const CAmount& contribution, const CKeyID& rewardAddr,const CKeyID& changeAddr)
{
    const auto& txin = CTxIn(prevTx.GetHash(),nOut);
    const auto& prevTxValueOut = prevTx.vout[nOut].nValue;

    CMutableTransaction mtx;
    // create a dummy input to fund the transaction
    mtx.vin.push_back(txin);

    // create a structured OP_RETURN output containing tx declaration
    BuyTicketData buyTicketData = { 1 };    // version
    CScript declScript = GetScriptForBuyTicketDecl(buyTicketData);
    mtx.vout.push_back(CTxOut(0, declScript));

    // create an output that pays dummy ticket stake
    mtx.vout.push_back(CTxOut(ticketPrice, stakeScript));

    // create an OP_RETURN push containing a dummy address to send rewards to, and the amount contributed to stake
    TicketContribData ticketContribData = { 1, rewardAddr, contribution };
    CScript contributorInfoScript = GetScriptForTicketContrib(ticketContribData);
    mtx.vout.push_back(CTxOut(0, contributorInfoScript));

    // create an output which pays back dummy change
    CScript changeScript = GetScriptForDestination(changeAddr);
    CAmount change = prevTxValueOut - contribution;
    mtx.vout.push_back(CTxOut(change, changeScript));

    return mtx;
}

CMutableTransaction CreateDummyVote(const uint256& txBuyTicketHash, const uint256& blockHashToVoteOn, const uint32_t& blockHeight, const CKeyID& rewardAddr, const CAmount& reward)
{
    CMutableTransaction mtx;

    // create a reward generation input
    mtx.vin.push_back(CTxIn(COutPoint(), CScript() << 55 << OP_0 ));

    mtx.vin.push_back(CTxIn(COutPoint(txBuyTicketHash, ticketStakeOutputIndex)));

    // create a structured OP_RETURN output containing tx declaration and dummy voting data
    uint32_t dummyVoteBits = 0x0001;
    VoteData voteData = { 1, blockHashToVoteOn, blockHeight, dummyVoteBits };
    CScript declScript = GetScriptForVoteDecl(voteData);
    mtx.vout.push_back(CTxOut(0, declScript));

    // Create an output which pays back a dummy reward
    CScript rewardScript = GetScriptForDestination(rewardAddr);
    mtx.vout.push_back(CTxOut(reward, rewardScript));

    return mtx;
}

CMutableTransaction CreateDummyRevokeTicket(const uint256& txBuyTicketHash, const CKeyID& rewardAddr, const CAmount& reward)
{
    CMutableTransaction mtx;

    // create an input from a dummy BuyTicket stake
    mtx.vin.push_back(CTxIn(COutPoint(txBuyTicketHash, ticketStakeOutputIndex)));

    // create a structured OP_RETURN output containing tx declaration
    RevokeTicketData revokeTicketData = { 1 };
    CScript declScript = GetScriptForRevokeTicketDecl(revokeTicketData);
    mtx.vout.push_back(CTxOut(0, declScript));

    // Create an output which pays back a dummy refund
    CScript rewardScript = GetScriptForDestination(rewardAddr);
    mtx.vout.push_back(CTxOut(reward, rewardScript));

    return mtx;
}

struct TestChain100Setup_p2pkh : public TestChain100Setup
{
    TestChain100Setup_p2pkh() : TestChain100Setup(scriptPubKeyType::P2PKH){
        ;
    }
};

BOOST_FIXTURE_TEST_CASE( CreateNewBlock_stake_REGTEST, TestChain100Setup_p2pkh)
{
    BOOST_CHECK_EQUAL(chainActive.Tip()->nHeight, 100);

    const auto& VOTES_PER_BLOCK   = 3;
    const auto& WINNERS_PER_BLOCK = 5;
    const auto& MISSES_PER_BLOCK  = WINNERS_PER_BLOCK - VOTES_PER_BLOCK;

    // Note that by default, these tests run with size accounting enabled.
    const CChainParams& chainparams = Params();
    const auto& stakeEnabledHeight = chainparams.GetConsensus().nStakeEnabledHeight;
    const auto& stakeValidationHeight = chainparams.GetConsensus().nStakeValidationHeight;
    const auto& minimumStakeDiff = chainparams.GetConsensus().nMinimumStakeDiff;
    const auto& ticketMaturity = chainparams.GetConsensus().nTicketMaturity;
    const auto& ticketExpiry = chainparams.GetConsensus().nTicketExpiry;
    const auto& maxFreshTicketsPerBlock = chainparams.GetConsensus().nMaxFreshStakePerBlock;

    // create a stake key
    auto stakeKey = CKey();
    stakeKey.MakeNewKey(true); // compressed as in TestChain100Setup
    // create a reward address
    auto rewardKey = CKey();
    rewardKey.MakeNewKey(false);
    const auto& rewardAddr = rewardKey.GetPubKey().GetID();
    // create a change address
    auto changeKey = CKey();
    changeKey.MakeNewKey(false);
    const auto& changeAddr = changeKey.GetPubKey().GetID();

    CScript scriptPubKeyCoinbase =
      CScript() << OP_DUP << OP_HASH160 << ToByteVector(coinbaseKey.GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    CScript scriptPubKeyStake    =
      CScript() << OP_DUP << OP_HASH160 << ToByteVector(stakeKey.GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    CScript scriptPubKeyChange   =
      CScript() << OP_DUP << OP_HASH160 << ToByteVector(changeAddr) << OP_EQUALVERIFY << OP_CHECKSIG;

    std::unique_ptr<CBlockTemplate> pblocktemplate;
    CMutableTransaction tx;
    CScript script;
    uint256 hash;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.nHeight = 11;

    fCheckpointsEnabled = false;

    // Simple block creation, nothing special yet:
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKeyCoinbase));

    // int baseheight = 0;

    LOCK(cs_main);
    LOCK(::mempool.cs);

    // Just to make sure we can still make simple blocks
    BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKeyCoinbase));

    // add more blocks, to get to 110, above the COINBASE_MATURITY to be able to spend first coinbases
    // TODO there are two params atm COINBASE_MATURITY (set to 100 in consensus.h) and consensus.nCoinbaseMaturity (set in 
    // chainparams.cpp , network dependent); we need to remove one of them 
    for( int i = 0 ; i < 10 ; ++i) {
        const auto& b = CreateAndProcessBlock({},scriptPubKeyCoinbase);
        coinbaseTxns.push_back(*b.vtx[0]);
    }
    BOOST_CHECK_EQUAL(chainActive.Tip()->nHeight, 110);

    while(chainActive.Tip()->nHeight < stakeEnabledHeight) {
        const auto& b = CreateAndProcessBlock({},scriptPubKeyCoinbase);
        coinbaseTxns.push_back(*b.vtx[0]);
    }

    BOOST_CHECK_GE(chainActive.Tip()->nHeight, stakeEnabledHeight);
    BOOST_CHECK_LT(chainActive.Tip()->nHeight, stakeValidationHeight);

    auto SignTx =[](CMutableTransaction& tx,unsigned int nIn, const CScript& scriptCode, const CKey& key)
    {
        std::vector<unsigned char> vchSig;
        uint256 hash = SignatureHash(scriptCode, tx, nIn, SIGHASH_ALL, 0, SIGVERSION_BASE);
        key.Sign(hash, vchSig);
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        tx.vin[nIn].scriptSig << vchSig << ToByteVector(key.GetPubKey());
    };

    auto ProcessTemplateBlock = [&chainparams](CBlock& block)
    {
        unsigned int extraNonce = 0;
        IncrementExtraNonce(&block, chainActive.Tip(), extraNonce);

        while (!CheckProofOfWork(block.GetHash(), block.nBits, chainparams.GetConsensus())) ++block.nNonce;

        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(block);
        ProcessNewBlock(chainparams, shared_pblock, true, nullptr);

        CBlock result = block;
        return result;
    };

    // PAICOIN Note: If the initial block subsidy has been changed,
    // update the subsidy with the correct value
    // const CAmount BLOCKSUBSIDY = chainparams.GetConsensus().nTotalBlockSubsidy * COIN;
    const CAmount ZEROFEE = 0LL;
    // const CAmount LOWFEE = CENT;
    // const CAmount HIGHFEE = COIN;
    // const CAmount HIGHERFEE = 4*COIN;

    // amount added to the current ticketPrice
    const auto& test_ticket_contributions = std::vector<CAmount>{
        1000LL,
        1100LL,
        100LL,
        20LL,
        200LL,
    }; 

    BOOST_CHECK_LE(test_ticket_contributions.size(), maxFreshTicketsPerBlock);

    auto spendCoinbaseIndex = 0UL;
    auto vBuyTicketTxs = std::vector<CMutableTransaction>{};
    auto vBoughtTickets = std::map<uint256, CAmount>{};

    auto BuyTestTickets = [&](const CAmount& ticketPrice, const std::vector<CAmount> contributions) {
        vBuyTicketTxs.clear();
        BOOST_CHECK_LT(spendCoinbaseIndex, coinbaseTxns.size());
        CMutableTransaction txBuyTicketFromCoinbase =
            CreateDummyBuyTicket(coinbaseTxns[spendCoinbaseIndex++], 0, ticketPrice, scriptPubKeyStake, ticketPrice+contributions[0], rewardAddr, changeAddr);
        SignTx(txBuyTicketFromCoinbase,0,scriptPubKeyCoinbase,coinbaseKey);
        vBuyTicketTxs.push_back(txBuyTicketFromCoinbase);
        vBoughtTickets[txBuyTicketFromCoinbase.GetHash()] = ticketPrice;
        mempool.addUnchecked(txBuyTicketFromCoinbase.GetHash(), entry.Fee(ZEROFEE).SpendsCoinbase(true).FromTx(txBuyTicketFromCoinbase));

        auto& nextToSpendTx = txBuyTicketFromCoinbase;

        for(auto i = 1UL; i < contributions.size(); ++i) {
            CMutableTransaction txBuyTicket =
                CreateDummyBuyTicket(nextToSpendTx, ticketChangeOutputIndex, ticketPrice, scriptPubKeyStake, ticketPrice+contributions[i], rewardAddr, changeAddr);
            SignTx(txBuyTicket,0,scriptPubKeyChange,changeKey);
            vBuyTicketTxs.push_back(txBuyTicket);
            vBoughtTickets[txBuyTicket.GetHash()] = ticketPrice;
            mempool.addUnchecked(txBuyTicket.GetHash(), entry.Fee(ZEROFEE).SpendsCoinbase(false).FromTx(txBuyTicket)); 

            nextToSpendTx = txBuyTicket;
        }
        BOOST_CHECK(pblocktemplate = AssemblerForTest(chainparams).CreateNewBlock(scriptPubKeyCoinbase));
        mempool.clear();
    };

    const auto firstBuyTicketMatureHeight = chainActive.Tip()->nHeight + ticketMaturity + 1;

    while (chainActive.Tip()->nHeight < firstBuyTicketMatureHeight) {
        BuyTestTickets(minimumStakeDiff, test_ticket_contributions);
        // actually add the block containing tickets as tip
        const auto& blockWithTickets = CreateAndProcessBlock(vBuyTicketTxs, scriptPubKeyCoinbase);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() == blockWithTickets.GetHash());
        coinbaseTxns.push_back(*blockWithTickets.vtx[0]);
    }

    BOOST_CHECK_EQUAL(chainActive.Tip()->nHeight, firstBuyTicketMatureHeight);
    // first buy has matured so we must have that number of live tickets
    BOOST_CHECK_EQUAL(chainActive.Tip()->pstakeNode->LiveTickets().size(), test_ticket_contributions.size());
    auto numberLiveTickets = chainActive.Tip()->pstakeNode->LiveTickets().size();

    while (chainActive.Tip()->nHeight <  stakeValidationHeight - 1) {
        // continue purchasing tickets until coinbase txs have been spend
        BuyTestTickets(minimumStakeDiff, test_ticket_contributions);
        const auto& blockWithTickets = CreateAndProcessBlock(vBuyTicketTxs, scriptPubKeyCoinbase);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() == blockWithTickets.GetHash());
        coinbaseTxns.push_back(*blockWithTickets.vtx[0]);
        numberLiveTickets += test_ticket_contributions.size();
    }

    BOOST_CHECK_EQUAL(chainActive.Tip()->nHeight, stakeValidationHeight - 1);
    BOOST_CHECK_EQUAL(chainActive.Tip()->pstakeNode->LiveTickets().size(), numberLiveTickets);

    {
        const auto& subsidy = GetVoterSubsidy(chainActive.Tip()->nHeight, Params().GetConsensus());
        const auto& reward = CalcContributorRemuneration(test_ticket_contributions[0], minimumStakeDiff, subsidy, test_ticket_contributions[0]);
        // create all needed transaction to obtain a block after stakeValidationHeight
        const auto& blockHeightToVoteOn = chainActive.Tip()->nHeight;
        const auto& blockHashToVoteOn = chainActive.Tip()->GetBlockHash();
        const auto& winningHashes = chainActive.Tip()->pstakeNode->Winners();

        BOOST_CHECK_EQUAL(winningHashes.size(), WINNERS_PER_BLOCK);
        // we will use 3 winners, thus 2 will be missed
        // add some good votes using the winning hashes and the hash of the tip
        for (auto i = 0; i < VOTES_PER_BLOCK; ++i) {
            CMutableTransaction txVote = CreateDummyVote(winningHashes[i], blockHashToVoteOn, blockHeightToVoteOn, rewardAddr, reward);
            SignTx(txVote, voteStakeInputIndex, scriptPubKeyStake, stakeKey);
            mempool.addUnchecked(txVote.GetHash(), entry.Fee(ZEROFEE).FromTx(txVote));
        }

        // add some bad votes
        // vote using dummy ticket hash
        CMutableTransaction txBadVote1 = CreateDummyVote(uint256(), blockHashToVoteOn, blockHeightToVoteOn, rewardAddr, reward);
        SignTx(txBadVote1, voteStakeInputIndex, scriptPubKeyStake, stakeKey);
        mempool.addUnchecked(txBadVote1.GetHash(), entry.Fee(ZEROFEE).FromTx(txBadVote1));
        // vote using dummy ticket hash and on dummy block hash
        CMutableTransaction txBadVote2 = CreateDummyVote(uint256(), uint256S(std::string("0xfedcba")), blockHeightToVoteOn, rewardAddr, reward);
        SignTx(txBadVote2, voteStakeInputIndex, scriptPubKeyStake, stakeKey);
        mempool.addUnchecked(txBadVote2.GetHash(), entry.Fee(ZEROFEE).FromTx(txBadVote2));

        const auto& ticketPrice = 1* COIN; // difficulty increased since we are at stakeValidationHeight
        BuyTestTickets(ticketPrice, test_ticket_contributions);
        // actually add the block using valid votes as tip
        const auto& blockWithVotes = ProcessTemplateBlock(pblocktemplate->block);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() == blockWithVotes.GetHash());
        coinbaseTxns.push_back(*blockWithVotes.vtx[0]);
    }

    numberLiveTickets += test_ticket_contributions.size()/*last number of matured*/ - 5/*used as winners in last block*/;
    BOOST_CHECK_EQUAL(chainActive.Tip()->pstakeNode->LiveTickets().size(), numberLiveTickets);

    BOOST_CHECK_LT(chainActive.Tip()->nHeight, ticketExpiry);

    const auto& heightExpiredBecomeMissed = (int)(ticketExpiry) + firstBuyTicketMatureHeight;

    while(chainActive.Tip()->nHeight < heightExpiredBecomeMissed + 10/*add to pass the expiration height*/) {
        // create a second block to include also revocations
        const auto& blockHeightToVoteOn = chainActive.Tip()->nHeight;
        const auto& blockHashToVoteOn = chainActive.Tip()->GetBlockHash();
        const auto& winningHashes = chainActive.Tip()->pstakeNode->Winners();

        // difficulty increased since we are above stakeValidationHeight
        const auto& ticketPrice = 1*COIN;
        const auto& subsidy = GetVoterSubsidy(chainActive.Tip()->nHeight+1 /*spend height*/, Params().GetConsensus());

        BOOST_CHECK_EQUAL(winningHashes.size(), WINNERS_PER_BLOCK);
        // add some good votes using the winning hashes and the hash of the tip
        for (auto i = 0; i < VOTES_PER_BLOCK; ++i) {
            const auto& ticketPriceAtPurchase = vBoughtTickets[winningHashes[i]];
            const auto& contributedAmount = ticketPriceAtPurchase + test_ticket_contributions[0];
            // recalculated reward
            const auto& reward = CalcContributorRemuneration( contributedAmount, ticketPriceAtPurchase, subsidy, contributedAmount);
            CMutableTransaction txVote = CreateDummyVote(winningHashes[i], blockHashToVoteOn, blockHeightToVoteOn, rewardAddr, reward);
            SignTx(txVote, voteStakeInputIndex, scriptPubKeyStake, stakeKey);
            mempool.addUnchecked(txVote.GetHash(), entry.Fee(ZEROFEE).FromTx(txVote));
        }

        const auto& missedHashes = chainActive.Tip()->pstakeNode->MissedTickets();
        numberLiveTickets += test_ticket_contributions.size()/*last number of matured*/ - VOTES_PER_BLOCK/*used as votes in last block*/ - missedHashes.size();
        BOOST_CHECK_EQUAL(chainActive.Tip()->pstakeNode->LiveTickets().size(), numberLiveTickets);
        if (blockHeightToVoteOn >= heightExpiredBecomeMissed) {
            // at this height some tickets might expired and be moved to missed, so we might have a greater number here
            BOOST_CHECK_GE(missedHashes.size(), MISSES_PER_BLOCK);
        }
        else {
        BOOST_CHECK_EQUAL(missedHashes.size(), MISSES_PER_BLOCK);
        }

        // add revocation transactions, using the last two tickets
        for (const auto& missedHash : missedHashes) {
            const auto& ticketPriceAtPurchase = vBoughtTickets[missedHash];
            CMutableTransaction txRevokeTicket = CreateDummyRevokeTicket(missedHash, rewardAddr, ticketPriceAtPurchase);
            SignTx(txRevokeTicket, revocationStakeInputIndex, scriptPubKeyStake, stakeKey);
            mempool.addUnchecked(txRevokeTicket.GetHash(), entry.Fee(ZEROFEE).SpendsCoinbase(true).FromTx(txRevokeTicket));
        }

        BuyTestTickets(ticketPrice, test_ticket_contributions);

        // actually add the block using valid votes as tip
        const auto& blockWithVotes = ProcessTemplateBlock(pblocktemplate->block);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() == blockWithVotes.GetHash());
        coinbaseTxns.push_back(*blockWithVotes.vtx[0]);
    }

    fCheckpointsEnabled = true;
}

BOOST_AUTO_TEST_SUITE_END()
