// Copyright (c) 2018-2025 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <xrouter/xroutermultinodechannel.h>

#include <rpc/server.h>
#include <rpc/util.h>
#include <util/strencodings.h>

#include <univalue.h>

namespace xrouter
{

// Global multi-node channel manager instance
extern XRouterMultiNodeChannelManager g_multiNodeChannelManager;

/**
 * @brief Convert ServiceNodeInfo to JSON
 */
static UniValue NodeInfoToJSON(const ServiceNodeInfo& nodeInfo)
{
    UniValue result(UniValue::VOBJ);

    result.pushKV("nodeAddress", nodeInfo.nodeAddress);
    result.pushKV("xrouterAddress", nodeInfo.xrouterAddress);
    result.pushKV("deposit", ValueFromAmount(nodeInfo.deposit));
    result.pushKV("balance", ValueFromAmount(nodeInfo.balance));
    result.pushKV("requestsServed", static_cast<uint64_t>(nodeInfo.requestsServed));
    result.pushKV("correctResponses", static_cast<uint64_t>(nodeInfo.correctResponses));
    result.pushKV("incorrectResponses", static_cast<uint64_t>(nodeInfo.incorrectResponses));
    result.pushKV("active", nodeInfo.active);

    // Calculate accuracy percentage
    if (nodeInfo.requestsServed > 0) {
        double accuracy = (static_cast<double>(nodeInfo.correctResponses) / nodeInfo.requestsServed) * 100.0;
        result.pushKV("accuracyPercentage", accuracy);
    } else {
        result.pushKV("accuracyPercentage", 100.0);
    }

    return result;
}

/**
 * @brief Convert MultiNodeChannel to JSON
 */
static UniValue ChannelToJSON(const MultiNodeChannel& channel)
{
    UniValue result(UniValue::VOBJ);

    result.pushKV("channelId", channel.channelId.GetHex());
    result.pushKV("contractAddress", channel.contractAddress);
    result.pushKV("clientAddress", channel.clientAddress);
    result.pushKV("clientDeposit", ValueFromAmount(channel.clientDeposit));
    result.pushKV("clientBalance", ValueFromAmount(channel.clientBalance));
    result.pushKV("nonce", static_cast<uint64_t>(channel.nonce));
    result.pushKV("challengePeriod", static_cast<uint64_t>(channel.challengePeriod));
    result.pushKV("openTime", static_cast<uint64_t>(channel.openTime));
    result.pushKV("closingTime", static_cast<uint64_t>(channel.closingTime));
    result.pushKV("minNodes", static_cast<uint64_t>(channel.minNodes));
    result.pushKV("quorumPercentage", static_cast<uint64_t>(channel.quorumPercentage));
    result.pushKV("totalRequests", static_cast<uint64_t>(channel.totalRequests));
    result.pushKV("activeNodeCount", static_cast<uint64_t>(channel.GetActiveNodeCount()));

    // Service nodes
    UniValue nodesArray(UniValue::VARR);
    for (const auto& nodeAddr : channel.serviceNodeAddresses) {
        auto it = channel.nodes.find(nodeAddr);
        if (it != channel.nodes.end()) {
            nodesArray.push_back(NodeInfoToJSON(it->second));
        }
    }
    result.pushKV("serviceNodes", nodesArray);

    // State
    std::string stateStr;
    switch (channel.state) {
        case ChannelState::OPEN: stateStr = "open"; break;
        case ChannelState::CHALLENGED: stateStr = "challenged"; break;
        case ChannelState::CLOSED: stateStr = "closed"; break;
        default: stateStr = "invalid"; break;
    }
    result.pushKV("state", stateStr);

    return result;
}

} // namespace xrouter

/**
 * RPC: xrOpenMultiNodeChannel
 *
 * Opens a new multi-node payment channel with consensus validation
 */
static UniValue xrOpenMultiNodeChannel(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 5)
        throw std::runtime_error(
            "xrOpenMultiNodeChannel [\"serviceNode1\",\"serviceNode2\",...] minNodes quorumPercentage amount ( challengePeriod )\n"
            "\nOpen a new multi-node payment channel with consensus validation.\n"
            "Only nodes that provide matching responses will receive payment.\n"
            "\nArguments:\n"
            "1. serviceNodes      (json array, required) Array of service node Ethereum addresses\n"
            "2. minNodes          (numeric, required) Minimum nodes required for consensus (min 2)\n"
            "3. quorumPercentage  (numeric, required) Percentage of nodes that must agree (51-100)\n"
            "4. amount            (numeric, required) Amount to deposit in BLOCK\n"
            "5. challengePeriod   (numeric, optional, default=3600) Challenge period in seconds\n"
            "\nResult:\n"
            "{\n"
            "  \"channelId\": \"xxx\",           (string) Unique channel identifier\n"
            "  \"serviceNodeCount\": n,          (numeric) Number of service nodes\n"
            "  \"minNodes\": n,                  (numeric) Minimum nodes for consensus\n"
            "  \"quorumPercentage\": n,          (numeric) Required consensus percentage\n"
            "  \"depositAmount\": x.xxx          (numeric) Amount deposited\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrOpenMultiNodeChannel", "'[\"0x1234...\",\"0x5678...\",\"0x9abc...\"]' 2 67 10.0")
            + HelpExampleRpc("xrOpenMultiNodeChannel", "[\"0x1234...\",\"0x5678...\",\"0x9abc...\"], 2, 67, 10.0")
        );

    // Parse service nodes array
    UniValue nodesArray = request.params[0].get_array();
    std::vector<std::string> serviceNodes;
    for (size_t i = 0; i < nodesArray.size(); i++) {
        serviceNodes.push_back(nodesArray[i].get_str());
    }

    uint64_t minNodes = request.params[1].get_int64();
    uint64_t quorumPercentage = request.params[2].get_int64();
    CAmount depositAmount = AmountFromValue(request.params[3]);
    uint64_t challengePeriod = request.params.size() > 4 ? request.params[4].get_int64() : 3600;

    uint256 channelId = xrouter::g_multiNodeChannelManager.OpenMultiNodeChannel(
        serviceNodes,
        minNodes,
        quorumPercentage,
        depositAmount,
        challengePeriod
    );

    if (channelId.IsNull()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to open multi-node channel");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("channelId", channelId.GetHex());
    result.pushKV("serviceNodeCount", static_cast<uint64_t>(serviceNodes.size()));
    result.pushKV("minNodes", minNodes);
    result.pushKV("quorumPercentage", quorumPercentage);
    result.pushKV("depositAmount", ValueFromAmount(depositAmount));
    result.pushKV("challengePeriod", challengePeriod);

    return result;
}

/**
 * RPC: xrSubmitMultiNodeRequest
 *
 * Submit a request to multiple service nodes with consensus validation
 */
static UniValue xrSubmitMultiNodeRequest(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
            "xrSubmitMultiNodeRequest \"channelId\" \"query\" totalFee\n"
            "\nSubmit a request to all nodes in a multi-node channel.\n"
            "Fee will be distributed only to nodes that provide matching responses.\n"
            "\nArguments:\n"
            "1. channelId    (string, required) Channel identifier\n"
            "2. query        (string, required) Query to send to all nodes\n"
            "3. totalFee     (numeric, required) Total fee to distribute among honest nodes\n"
            "\nResult:\n"
            "{\n"
            "  \"requestId\": \"xxx\",      (string) Unique request identifier\n"
            "  \"channelId\": \"xxx\",      (string) Channel identifier\n"
            "  \"query\": \"xxx\",          (string) Query sent\n"
            "  \"totalFee\": x.xxx,         (numeric) Total fee\n"
            "  \"nodeCount\": n             (numeric) Number of nodes queried\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrSubmitMultiNodeRequest", "\"abc123...\" \"xrGetBlockCount BTC\" 0.03")
            + HelpExampleRpc("xrSubmitMultiNodeRequest", "\"abc123...\", \"xrGetBlockCount BTC\", 0.03")
        );

    uint256 channelId;
    channelId.SetHex(request.params[0].get_str());

    std::string query = request.params[1].get_str();
    CAmount totalFee = AmountFromValue(request.params[2]);

    uint256 requestId;
    if (!xrouter::g_multiNodeChannelManager.SubmitMultiNodeRequest(channelId, query, totalFee, requestId)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to submit multi-node request");
    }

    auto channel = xrouter::g_multiNodeChannelManager.GetChannel(channelId);

    UniValue result(UniValue::VOBJ);
    result.pushKV("requestId", requestId.GetHex());
    result.pushKV("channelId", channelId.GetHex());
    result.pushKV("query", query);
    result.pushKV("totalFee", ValueFromAmount(totalFee));
    if (channel) {
        result.pushKV("nodeCount", static_cast<uint64_t>(channel->GetActiveNodeCount()));
    }

    return result;
}

/**
 * RPC: xrGetConsensusResponse
 *
 * Get the consensus response for a request
 */
static UniValue xrGetConsensusResponse(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "xrGetConsensusResponse \"channelId\" \"requestId\"\n"
            "\nGet the consensus response from multiple service nodes.\n"
            "\nArguments:\n"
            "1. channelId    (string, required) Channel identifier\n"
            "2. requestId    (string, required) Request identifier\n"
            "\nResult:\n"
            "{\n"
            "  \"requestId\": \"xxx\",          (string) Request identifier\n"
            "  \"consensusReached\": true/false, (boolean) Whether consensus was reached\n"
            "  \"consensusResponse\": \"xxx\",  (string) The consensus response data\n"
            "  \"honestNodeCount\": n,          (numeric) Number of nodes with matching response\n"
            "  \"dishonestNodeCount\": n,       (numeric) Number of nodes with different response\n"
            "  \"settled\": true/false          (boolean) Whether payment has been distributed\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrGetConsensusResponse", "\"abc123...\" \"def456...\"")
            + HelpExampleRpc("xrGetConsensusResponse", "\"abc123...\", \"def456...\"")
        );

    uint256 channelId;
    channelId.SetHex(request.params[0].get_str());

    uint256 requestId;
    requestId.SetHex(request.params[1].get_str());

    bool consensusReached = xrouter::g_multiNodeChannelManager.IsConsensusReached(channelId, requestId);
    std::string consensusResponse = xrouter::g_multiNodeChannelManager.GetConsensusResponse(channelId, requestId);

    auto channel = xrouter::g_multiNodeChannelManager.GetChannel(channelId);
    if (!channel) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Channel not found");
    }

    auto reqIt = channel->requests.find(requestId);
    if (reqIt == channel->requests.end()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Request not found");
    }

    const auto& req = reqIt->second;

    UniValue result(UniValue::VOBJ);
    result.pushKV("requestId", requestId.GetHex());
    result.pushKV("consensusReached", consensusReached);
    result.pushKV("consensusResponse", consensusResponse);
    result.pushKV("honestNodeCount", static_cast<uint64_t>(req.honestNodes.size()));
    result.pushKV("dishonestNodeCount", static_cast<uint64_t>(req.dishonestNodes.size()));
    result.pushKV("totalResponses", static_cast<uint64_t>(req.responses.size()));
    result.pushKV("settled", req.settled);

    // List honest and dishonest nodes
    UniValue honestArray(UniValue::VARR);
    for (const auto& addr : req.honestNodes) {
        honestArray.push_back(addr);
    }
    result.pushKV("honestNodes", honestArray);

    UniValue dishonestArray(UniValue::VARR);
    for (const auto& addr : req.dishonestNodes) {
        dishonestArray.push_back(addr);
    }
    result.pushKV("dishonestNodes", dishonestArray);

    return result;
}

/**
 * RPC: xrSettleMultiNodeRequest
 *
 * Settle a request and distribute payment to honest nodes
 */
static UniValue xrSettleMultiNodeRequest(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "xrSettleMultiNodeRequest \"channelId\" \"requestId\"\n"
            "\nSettle a multi-node request and distribute payment to honest nodes.\n"
            "Only nodes that provided the consensus response will be paid.\n"
            "\nArguments:\n"
            "1. channelId    (string, required) Channel identifier\n"
            "2. requestId    (string, required) Request identifier\n"
            "\nResult:\n"
            "{\n"
            "  \"settled\": true,               (boolean) Always true if successful\n"
            "  \"honestNodes\": [...],          (array) Addresses of honest nodes paid\n"
            "  \"feePerNode\": x.xxx            (numeric) Amount paid to each honest node\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrSettleMultiNodeRequest", "\"abc123...\" \"def456...\"")
            + HelpExampleRpc("xrSettleMultiNodeRequest", "\"abc123...\", \"def456...\"")
        );

    uint256 channelId;
    channelId.SetHex(request.params[0].get_str());

    uint256 requestId;
    requestId.SetHex(request.params[1].get_str());

    auto channel = xrouter::g_multiNodeChannelManager.GetChannel(channelId);
    if (!channel) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Channel not found");
    }

    auto reqIt = channel->requests.find(requestId);
    if (reqIt == channel->requests.end()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Request not found");
    }

    const auto& req = reqIt->second;
    CAmount totalFee = req.totalFee;

    if (!xrouter::g_multiNodeChannelManager.SettleRequest(channelId, requestId)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to settle request");
    }

    // Get updated request
    const auto& settledReq = channel->requests[requestId];

    CAmount feePerNode = settledReq.honestNodes.size() > 0 ?
        totalFee / settledReq.honestNodes.size() : 0;

    UniValue honestArray(UniValue::VARR);
    for (const auto& addr : settledReq.honestNodes) {
        honestArray.push_back(addr);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("settled", true);
    result.pushKV("honestNodes", honestArray);
    result.pushKV("feePerNode", ValueFromAmount(feePerNode));
    result.pushKV("totalFee", ValueFromAmount(totalFee));

    return result;
}

/**
 * RPC: xrGetMultiNodeChannel
 *
 * Get information about a multi-node channel
 */
static UniValue xrGetMultiNodeChannel(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "xrGetMultiNodeChannel \"channelId\"\n"
            "\nGet detailed information about a multi-node payment channel.\n"
            "\nArguments:\n"
            "1. channelId    (string, required) Channel identifier\n"
            "\nResult:\n"
            "{\n"
            "  \"channelId\": \"xxx\",\n"
            "  \"clientBalance\": x.xxx,\n"
            "  \"minNodes\": n,\n"
            "  \"quorumPercentage\": n,\n"
            "  \"totalRequests\": n,\n"
            "  \"serviceNodes\": [...],\n"
            "  ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrGetMultiNodeChannel", "\"abc123...\"")
            + HelpExampleRpc("xrGetMultiNodeChannel", "\"abc123...\"")
        );

    uint256 channelId;
    channelId.SetHex(request.params[0].get_str());

    auto channel = xrouter::g_multiNodeChannelManager.GetChannel(channelId);
    if (!channel) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Channel not found");
    }

    return xrouter::ChannelToJSON(*channel);
}

/**
 * RPC: xrGetNodeStats
 *
 * Get statistics for a service node in a channel
 */
static UniValue xrGetNodeStats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "xrGetNodeStats \"channelId\" \"nodeAddress\"\n"
            "\nGet performance statistics for a service node in a channel.\n"
            "\nArguments:\n"
            "1. channelId     (string, required) Channel identifier\n"
            "2. nodeAddress   (string, required) Node's Ethereum address\n"
            "\nResult:\n"
            "{\n"
            "  \"nodeAddress\": \"xxx\",\n"
            "  \"balance\": x.xxx,\n"
            "  \"requestsServed\": n,\n"
            "  \"correctResponses\": n,\n"
            "  \"incorrectResponses\": n,\n"
            "  \"accuracyPercentage\": x.xx,\n"
            "  \"active\": true/false\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrGetNodeStats", "\"abc123...\" \"0x1234...\"")
            + HelpExampleRpc("xrGetNodeStats", "\"abc123...\", \"0x1234...\"")
        );

    uint256 channelId;
    channelId.SetHex(request.params[0].get_str());

    std::string nodeAddress = request.params[1].get_str();

    auto nodeInfo = xrouter::g_multiNodeChannelManager.GetNodeStats(channelId, nodeAddress);
    if (!nodeInfo) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Node not found in channel");
    }

    return xrouter::NodeInfoToJSON(*nodeInfo);
}

/**
 * RPC: xrListMultiNodeChannels
 *
 * List all multi-node channels
 */
static UniValue xrListMultiNodeChannels(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "xrListMultiNodeChannels\n"
            "\nList all multi-node payment channels.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"channelId\": \"xxx\",\n"
            "    \"nodeCount\": n,\n"
            "    \"clientBalance\": x.xxx,\n"
            "    ...\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("xrListMultiNodeChannels", "")
            + HelpExampleRpc("xrListMultiNodeChannels", "")
        );

    std::vector<uint256> channelIds = xrouter::g_multiNodeChannelManager.GetOpenChannels();

    UniValue result(UniValue::VARR);

    for (const auto& channelId : channelIds) {
        auto channel = xrouter::g_multiNodeChannelManager.GetChannel(channelId);
        if (channel) {
            result.push_back(xrouter::ChannelToJSON(*channel));
        }
    }

    return result;
}

// Register RPC commands
static const CRPCCommand commands[] = {
    { "xrouter", "xrOpenMultiNodeChannel",     &xrOpenMultiNodeChannel,    {"serviceNodes", "minNodes", "quorumPercentage", "amount", "challengePeriod"} },
    { "xrouter", "xrSubmitMultiNodeRequest",   &xrSubmitMultiNodeRequest,  {"channelId", "query", "totalFee"} },
    { "xrouter", "xrGetConsensusResponse",     &xrGetConsensusResponse,    {"channelId", "requestId"} },
    { "xrouter", "xrSettleMultiNodeRequest",   &xrSettleMultiNodeRequest,  {"channelId", "requestId"} },
    { "xrouter", "xrGetMultiNodeChannel",      &xrGetMultiNodeChannel,     {"channelId"} },
    { "xrouter", "xrGetNodeStats",             &xrGetNodeStats,            {"channelId", "nodeAddress"} },
    { "xrouter", "xrListMultiNodeChannels",    &xrListMultiNodeChannels,   {} },
};

void RegisterMultiNodeChannelRPCCommands(CRPCTable &t)
{
    for (const auto& command : commands) {
        t.appendCommand(command.name, &command);
    }
}
