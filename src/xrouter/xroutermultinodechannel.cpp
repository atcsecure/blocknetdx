// Copyright (c) 2018-2025 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <xrouter/xroutermultinodechannel.h>
#include <xrouter/xrouterpaymentchannel.h>

#include <hash.h>
#include <streams.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <logging.h>

#include <algorithm>

namespace xrouter
{

//******************************************************************************
// NodeResponse implementation
//******************************************************************************

uint256 NodeResponse::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << nodeAddress;
    ss << responseData;
    ss << timestamp;
    return ss.GetHash();
}

bool NodeResponse::VerifySignature(const CPubKey& pubKey) const
{
    uint256 hash = responseHash;

    if (!pubKey.Verify(hash, signature)) {
        LogPrint(BCLog::XROUTER, "XRouter: Invalid signature for response from %s\n", nodeAddress);
        return false;
    }

    return true;
}

//******************************************************************************
// MultiNodeRequest implementation
//******************************************************************************

bool MultiNodeRequest::AddResponse(const NodeResponse& response)
{
    // Check for duplicate response from same node
    for (const auto& existing : responses) {
        if (existing.nodeAddress == response.nodeAddress) {
            LogPrint(BCLog::XROUTER, "XRouter: Duplicate response from node %s\n", response.nodeAddress);
            return false;
        }
    }

    // Add response
    responses.push_back(response);

    // Update vote count
    responseVotes[response.responseHash]++;

    return true;
}

bool MultiNodeRequest::CalculateConsensus(uint64_t quorumPercentage)
{
    if (responses.empty()) {
        return false;
    }

    // Find hash with most votes
    uint256 maxHash;
    uint64_t maxVotes = 0;

    for (const auto& pair : responseVotes) {
        if (pair.second > maxVotes) {
            maxHash = pair.first;
            maxVotes = pair.second;
        }
    }

    consensusHash = maxHash;
    consensusCount = maxVotes;

    // Check if quorum reached
    uint64_t requiredVotes = (responses.size() * quorumPercentage) / 100;

    if (consensusCount < requiredVotes) {
        LogPrint(BCLog::XROUTER, "XRouter: Consensus not reached (%d/%d votes, need %d)\n",
            consensusCount, responses.size(), requiredVotes);
        return false;
    }

    // Identify honest and dishonest nodes
    honestNodes.clear();
    dishonestNodes.clear();

    for (const auto& response : responses) {
        if (response.responseHash == consensusHash) {
            honestNodes.insert(response.nodeAddress);
        } else {
            dishonestNodes.insert(response.nodeAddress);
        }
    }

    LogPrint(BCLog::XROUTER, "XRouter: Consensus reached with %d/%d nodes, %d honest, %d dishonest\n",
        consensusCount, responses.size(), honestNodes.size(), dishonestNodes.size());

    return true;
}

std::string MultiNodeRequest::GetConsensusResponse() const
{
    if (consensusHash.IsNull()) {
        return "";
    }

    // Find response with consensus hash
    for (const auto& response : responses) {
        if (response.responseHash == consensusHash) {
            return response.responseData;
        }
    }

    return "";
}

//******************************************************************************
// MultiNodeChannel implementation
//******************************************************************************

uint64_t MultiNodeChannel::GetActiveNodeCount() const
{
    uint64_t count = 0;
    for (const auto& addr : serviceNodeAddresses) {
        auto it = nodes.find(addr);
        if (it != nodes.end() && it->second.active) {
            count++;
        }
    }
    return count;
}

//******************************************************************************
// XRouterMultiNodeChannelManager implementation
//******************************************************************************

bool XRouterMultiNodeChannelManager::Init(const std::string& ethereumRpcUrl, const std::string& contractAddress)
{
    ethereumRpcUrl_ = ethereumRpcUrl;
    contractAddress_ = contractAddress;

    if (!LoadChannels()) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to load multi-node channels\n");
        return false;
    }

    LogPrint(BCLog::XROUTER, "XRouter: Multi-node channel manager initialized with %d channels\n", channels_.size());
    return true;
}

uint256 XRouterMultiNodeChannelManager::OpenMultiNodeChannel(
    const std::vector<std::string>& serviceNodes,
    uint64_t minNodes,
    uint64_t quorumPercentage,
    CAmount depositAmount,
    uint64_t challengePeriod)
{
    if (serviceNodes.size() < minNodes) {
        LogPrint(BCLog::XROUTER, "XRouter: Not enough service nodes (%d < %d)\n", serviceNodes.size(), minNodes);
        return uint256();
    }

    if (minNodes < 2) {
        LogPrint(BCLog::XROUTER, "XRouter: Minimum 2 nodes required\n");
        return uint256();
    }

    if (quorumPercentage < 51 || quorumPercentage > 100) {
        LogPrint(BCLog::XROUTER, "XRouter: Quorum percentage must be 51-100\n");
        return uint256();
    }

    if (depositAmount <= 0) {
        LogPrint(BCLog::XROUTER, "XRouter: Invalid deposit amount\n");
        return uint256();
    }

    // Generate channel ID
    CHashWriter ss(SER_GETHASH, 0);
    ss << clientEthAddress_;
    for (const auto& node : serviceNodes) {
        ss << node;
    }
    ss << GetTime();
    ss << GetRand(std::numeric_limits<uint64_t>::max());
    uint256 channelId = ss.GetHash();

    // Create channel
    auto channel = std::make_shared<MultiNodeChannel>();
    channel->channelId = channelId;
    channel->contractAddress = contractAddress_;
    channel->clientAddress = clientEthAddress_;
    channel->clientPubKey = clientKey_.GetPubKey();
    channel->clientDeposit = depositAmount;
    channel->clientBalance = depositAmount;
    channel->nonce = 0;
    channel->challengePeriod = challengePeriod;
    channel->openTime = GetTime();
    channel->closingTime = 0;
    channel->minNodes = minNodes;
    channel->quorumPercentage = quorumPercentage;
    channel->totalRequests = 0;
    channel->state = ChannelState::OPEN;

    // Add service nodes
    for (const auto& nodeAddr : serviceNodes) {
        channel->serviceNodeAddresses.push_back(nodeAddr);

        ServiceNodeInfo nodeInfo;
        nodeInfo.nodeAddress = nodeAddr;
        nodeInfo.active = true;

        channel->nodes[nodeAddr] = nodeInfo;
    }

    // Call smart contract
    std::vector<std::string> params;
    // Build service node array param
    std::string nodesParam = "[";
    for (size_t i = 0; i < serviceNodes.size(); i++) {
        if (i > 0) nodesParam += ",";
        nodesParam += "\"" + serviceNodes[i] + "\"";
    }
    nodesParam += "]";

    params.push_back(nodesParam);
    params.push_back(std::to_string(minNodes));
    params.push_back(std::to_string(quorumPercentage));
    params.push_back(std::to_string(challengePeriod));

    std::string txHash;
    if (!SendContractTransaction("openMultiNodeChannel", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to open multi-node channel on blockchain\n");
        return uint256();
    }

    // Store channel
    channels_[channelId] = channel;
    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Opened multi-node channel %s with %d nodes\n",
        channelId.GetHex(), serviceNodes.size());

    return channelId;
}

bool XRouterMultiNodeChannelManager::SubmitMultiNodeRequest(
    const uint256& channelId,
    const std::string& query,
    CAmount totalFee,
    uint256& outRequestId)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    if (!channel->IsOpen()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not open\n", channelId.GetHex());
        return false;
    }

    if (channel->clientBalance < totalFee) {
        LogPrint(BCLog::XROUTER, "XRouter: Insufficient balance in channel %s\n", channelId.GetHex());
        return false;
    }

    if (!channel->HasSufficientNodes()) {
        LogPrint(BCLog::XROUTER, "XRouter: Not enough active nodes in channel\n");
        return false;
    }

    // Generate request ID
    outRequestId = GenerateRequestId(channelId, query);

    // Create request
    MultiNodeRequest request;
    request.requestId = outRequestId;
    request.query = query;
    request.queryHash = Hash(query.begin(), query.end());
    request.totalFee = totalFee;
    request.timestamp = GetTime();
    request.settled = false;

    // Store request
    channel->requests[outRequestId] = request;
    channel->totalRequests++;

    // Call smart contract to escrow fee
    std::vector<std::string> params;
    params.push_back(channelId.GetHex());
    params.push_back(outRequestId.GetHex());
    params.push_back(request.queryHash.GetHex());
    params.push_back(std::to_string(totalFee));

    std::string txHash;
    if (!SendContractTransaction("submitRequest", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to submit request to blockchain\n");
        return false;
    }

    // Deduct fee from balance (held in escrow)
    channel->clientBalance -= totalFee;
    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Submitted request %s to channel %s with fee %d\n",
        outRequestId.GetHex(), channelId.GetHex(), totalFee);

    return true;
}

bool XRouterMultiNodeChannelManager::AddNodeResponse(
    const uint256& channelId,
    const uint256& requestId,
    const NodeResponse& response)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    auto reqIt = channel->requests.find(requestId);
    if (reqIt == channel->requests.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Request %s not found\n", requestId.GetHex());
        return false;
    }

    MultiNodeRequest& request = reqIt->second;

    if (request.settled) {
        LogPrint(BCLog::XROUTER, "XRouter: Request %s already settled\n", requestId.GetHex());
        return false;
    }

    // Verify node is part of channel
    if (channel->nodes.find(response.nodeAddress) == channel->nodes.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Node %s not part of channel\n", response.nodeAddress);
        return false;
    }

    // Verify node is active
    if (!channel->nodes[response.nodeAddress].active) {
        LogPrint(BCLog::XROUTER, "XRouter: Node %s not active\n", response.nodeAddress);
        return false;
    }

    // Add response
    if (!request.AddResponse(response)) {
        return false;
    }

    // Submit response hash to smart contract
    std::vector<std::string> params;
    params.push_back(channelId.GetHex());
    params.push_back(requestId.GetHex());
    params.push_back(response.responseHash.GetHex());

    std::string txHash;
    if (!SendContractTransaction("submitResponse", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to submit response to blockchain\n");
        // Continue anyway, we have it locally
    }

    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Added response from %s for request %s (%d/%d responses)\n",
        response.nodeAddress, requestId.GetHex(), request.responses.size(), channel->GetActiveNodeCount());

    return true;
}

bool XRouterMultiNodeChannelManager::IsConsensusReached(
    const uint256& channelId,
    const uint256& requestId)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        return false;
    }

    auto channel = it->second;

    auto reqIt = channel->requests.find(requestId);
    if (reqIt == channel->requests.end()) {
        return false;
    }

    MultiNodeRequest& request = reqIt->second;

    // Calculate consensus
    return request.CalculateConsensus(channel->quorumPercentage);
}

bool XRouterMultiNodeChannelManager::SettleRequest(
    const uint256& channelId,
    const uint256& requestId)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    auto reqIt = channel->requests.find(requestId);
    if (reqIt == channel->requests.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Request %s not found\n", requestId.GetHex());
        return false;
    }

    MultiNodeRequest& request = reqIt->second;

    if (request.settled) {
        LogPrint(BCLog::XROUTER, "XRouter: Request %s already settled\n", requestId.GetHex());
        return false;
    }

    // Ensure consensus calculated
    if (!request.CalculateConsensus(channel->quorumPercentage)) {
        LogPrint(BCLog::XROUTER, "XRouter: Consensus not reached for request %s\n", requestId.GetHex());
        return false;
    }

    // Call smart contract to settle
    std::vector<std::string> params;
    params.push_back(channelId.GetHex());
    params.push_back(requestId.GetHex());

    std::string txHash;
    if (!SendContractTransaction("settleRequest", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to settle request on blockchain\n");
        return false;
    }

    // Update local state
    request.settled = true;

    // Update node statistics
    CAmount feePerNode = request.totalFee / request.honestNodes.size();

    for (const auto& nodeAddr : request.honestNodes) {
        auto& nodeInfo = channel->nodes[nodeAddr];
        nodeInfo.balance += feePerNode;
        nodeInfo.requestsServed++;
        nodeInfo.correctResponses++;
    }

    for (const auto& nodeAddr : request.dishonestNodes) {
        auto& nodeInfo = channel->nodes[nodeAddr];
        nodeInfo.incorrectResponses++;
    }

    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Settled request %s, paid %d to %d honest nodes\n",
        requestId.GetHex(), feePerNode, request.honestNodes.size());

    return true;
}

std::string XRouterMultiNodeChannelManager::GetConsensusResponse(
    const uint256& channelId,
    const uint256& requestId)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        return "";
    }

    auto channel = it->second;

    auto reqIt = channel->requests.find(requestId);
    if (reqIt == channel->requests.end()) {
        return "";
    }

    return reqIt->second.GetConsensusResponse();
}

std::shared_ptr<ServiceNodeInfo> XRouterMultiNodeChannelManager::GetNodeStats(
    const uint256& channelId,
    const std::string& nodeAddress)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        return nullptr;
    }

    auto channel = it->second;

    auto nodeIt = channel->nodes.find(nodeAddress);
    if (nodeIt == channel->nodes.end()) {
        return nullptr;
    }

    return std::make_shared<ServiceNodeInfo>(nodeIt->second);
}

bool XRouterMultiNodeChannelManager::CloseChannel(const uint256& channelId)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    if (!channel->IsOpen()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not open\n", channelId.GetHex());
        return false;
    }

    // Call smart contract
    std::vector<std::string> params;
    params.push_back(channelId.GetHex());

    std::string txHash;
    if (!SendContractTransaction("initiateClose", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to initiate close on blockchain\n");
        return false;
    }

    channel->state = ChannelState::CHALLENGED;
    channel->closingTime = GetTime();
    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Initiated close for channel %s\n", channelId.GetHex());

    return true;
}

bool XRouterMultiNodeChannelManager::FinalizeClose(const uint256& channelId)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    if (channel->state != ChannelState::CHALLENGED) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not in challenged state\n", channelId.GetHex());
        return false;
    }

    uint64_t now = GetTime();
    if (now < channel->closingTime + channel->challengePeriod) {
        LogPrint(BCLog::XROUTER, "XRouter: Challenge period not expired\n");
        return false;
    }

    // Call smart contract
    std::vector<std::string> params;
    params.push_back(channelId.GetHex());

    std::string txHash;
    if (!SendContractTransaction("finalizeClose", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to finalize close on blockchain\n");
        return false;
    }

    channel->state = ChannelState::CLOSED;
    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Finalized close for channel %s\n", channelId.GetHex());

    return true;
}

std::shared_ptr<MultiNodeChannel> XRouterMultiNodeChannelManager::GetChannel(const uint256& channelId)
{
    auto it = channels_.find(channelId);
    if (it != channels_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<uint256> XRouterMultiNodeChannelManager::GetOpenChannels()
{
    std::vector<uint256> result;

    for (const auto& pair : channels_) {
        if (pair.second->IsOpen()) {
            result.push_back(pair.first);
        }
    }

    return result;
}

bool XRouterMultiNodeChannelManager::WithdrawNodeEarnings(const uint256& channelId, CAmount amount)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    // Find node in channel (assuming caller is a service node)
    // In real implementation, would verify caller identity

    std::vector<std::string> params;
    params.push_back(channelId.GetHex());
    params.push_back(std::to_string(amount));

    std::string txHash;
    if (!SendContractTransaction("withdrawNodeEarnings", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to withdraw earnings\n");
        return false;
    }

    return true;
}

//******************************************************************************
// Private methods
//******************************************************************************

uint256 XRouterMultiNodeChannelManager::GenerateRequestId(const uint256& channelId, const std::string& query)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << channelId;
    ss << query;
    ss << GetTime();
    ss << GetRand(std::numeric_limits<uint64_t>::max());
    return ss.GetHash();
}

bool XRouterMultiNodeChannelManager::CallContractMethod(
    const std::string& method,
    const std::vector<std::string>& params,
    std::string& result)
{
    // TODO: Implement Ethereum RPC call
    LogPrint(BCLog::XROUTER, "XRouter: Calling contract method %s\n", method);
    return true;
}

bool XRouterMultiNodeChannelManager::SendContractTransaction(
    const std::string& method,
    const std::vector<std::string>& params,
    std::string& txHash)
{
    // TODO: Implement Ethereum transaction sending
    LogPrint(BCLog::XROUTER, "XRouter: Sending contract transaction for %s\n", method);
    txHash = "0x" + HexStr(std::vector<unsigned char>(32, 0));
    return true;
}

bool XRouterMultiNodeChannelManager::LoadChannels()
{
    // TODO: Implement persistent storage
    channels_.clear();
    return true;
}

bool XRouterMultiNodeChannelManager::SaveChannel(const MultiNodeChannel& channel)
{
    // TODO: Implement persistent storage
    return true;
}

} // namespace xrouter
