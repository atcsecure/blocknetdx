// Copyright (c) 2018-2025 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <xrouter/xrouterpaymentchannel.h>

#include <rpc/server.h>
#include <rpc/util.h>
#include <util/strencodings.h>

#include <univalue.h>

namespace xrouter
{

// Global payment channel manager instance
extern XRouterPaymentChannelManager g_paymentChannelManager;

/**
 * @brief Convert channel state enum to string
 */
static std::string ChannelStateToString(ChannelState state)
{
    switch (state) {
        case ChannelState::OPEN: return "open";
        case ChannelState::CHALLENGED: return "challenged";
        case ChannelState::CLOSED: return "closed";
        case ChannelState::INVALID: return "invalid";
        default: return "unknown";
    }
}

/**
 * @brief Convert PaymentChannel to JSON
 */
static UniValue ChannelToJSON(const PaymentChannel& channel)
{
    UniValue result(UniValue::VOBJ);

    result.pushKV("channelId", channel.channelId.GetHex());
    result.pushKV("contractAddress", channel.contractAddress);
    result.pushKV("clientAddress", channel.clientAddress);
    result.pushKV("serviceNodeAddress", channel.serviceNodeAddress);
    result.pushKV("clientDeposit", ValueFromAmount(channel.clientDeposit));
    result.pushKV("serviceNodeDeposit", ValueFromAmount(channel.serviceNodeDeposit));
    result.pushKV("clientBalance", ValueFromAmount(channel.clientBalance));
    result.pushKV("serviceNodeBalance", ValueFromAmount(channel.serviceNodeBalance));
    result.pushKV("nonce", static_cast<uint64_t>(channel.nonce));
    result.pushKV("challengePeriod", static_cast<uint64_t>(channel.challengePeriod));
    result.pushKV("openTime", static_cast<uint64_t>(channel.openTime));
    result.pushKV("closingTime", static_cast<uint64_t>(channel.closingTime));
    result.pushKV("state", ChannelStateToString(channel.state));
    result.pushKV("capacity", ValueFromAmount(channel.GetCapacity()));
    result.pushKV("availableBalance", ValueFromAmount(channel.GetAvailableBalance()));

    return result;
}

/**
 * @brief Convert ChannelStateUpdate to JSON
 */
static UniValue StateUpdateToJSON(const ChannelStateUpdate& state)
{
    UniValue result(UniValue::VOBJ);

    result.pushKV("channelId", state.channelId.GetHex());
    result.pushKV("nonce", static_cast<uint64_t>(state.nonce));
    result.pushKV("clientBalance", ValueFromAmount(state.clientBalance));
    result.pushKV("serviceNodeBalance", ValueFromAmount(state.serviceNodeBalance));
    result.pushKV("clientSignature", HexStr(state.clientSignature));
    result.pushKV("serviceNodeSignature", HexStr(state.serviceNodeSignature));
    result.pushKV("timestamp", static_cast<uint64_t>(state.timestamp));
    result.pushKV("hash", state.GetHash().GetHex());

    return result;
}

} // namespace xrouter

/**
 * RPC: xrOpenPaymentChannel
 *
 * Opens a new payment channel with a service node
 */
static UniValue xrOpenPaymentChannel(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "xrOpenPaymentChannel \"serviceNodeAddress\" amount ( challengePeriod )\n"
            "\nOpen a new payment channel with a service node for off-chain XRouter payments.\n"
            "\nArguments:\n"
            "1. serviceNodeAddress    (string, required) Service node's Ethereum address\n"
            "2. amount                (numeric, required) Amount to deposit in BLOCK\n"
            "3. challengePeriod       (numeric, optional, default=3600) Challenge period in seconds\n"
            "\nResult:\n"
            "{\n"
            "  \"channelId\": \"xxx\",           (string) Unique channel identifier\n"
            "  \"contractAddress\": \"xxx\",     (string) Smart contract address\n"
            "  \"serviceNodeAddress\": \"xxx\",  (string) Service node's address\n"
            "  \"depositAmount\": x.xxx,         (numeric) Amount deposited\n"
            "  \"challengePeriod\": xxx          (numeric) Challenge period in seconds\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrOpenPaymentChannel", "\"0x1234...\" 10.0")
            + HelpExampleRpc("xrOpenPaymentChannel", "\"0x1234...\", 10.0")
        );

    std::string serviceNodeAddress = request.params[0].get_str();
    CAmount depositAmount = AmountFromValue(request.params[1]);
    uint64_t challengePeriod = request.params.size() > 2 ? request.params[2].get_int64() : 3600;

    // TODO: Get service node public key (from XRouter peer manager or configuration)
    CPubKey serviceNodePubKey; // Placeholder

    uint256 channelId = xrouter::g_paymentChannelManager.OpenChannel(
        serviceNodeAddress,
        serviceNodePubKey,
        depositAmount,
        challengePeriod
    );

    if (channelId.IsNull()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to open payment channel");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("channelId", channelId.GetHex());
    result.pushKV("serviceNodeAddress", serviceNodeAddress);
    result.pushKV("depositAmount", ValueFromAmount(depositAmount));
    result.pushKV("challengePeriod", challengePeriod);

    return result;
}

/**
 * RPC: xrGetPaymentChannel
 *
 * Get information about a payment channel
 */
static UniValue xrGetPaymentChannel(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "xrGetPaymentChannel \"channelId\"\n"
            "\nGet detailed information about a payment channel.\n"
            "\nArguments:\n"
            "1. channelId    (string, required) Channel identifier\n"
            "\nResult:\n"
            "{\n"
            "  \"channelId\": \"xxx\",              (string) Channel identifier\n"
            "  \"contractAddress\": \"xxx\",        (string) Smart contract address\n"
            "  \"clientAddress\": \"xxx\",          (string) Client's address\n"
            "  \"serviceNodeAddress\": \"xxx\",     (string) Service node's address\n"
            "  \"clientDeposit\": x.xxx,            (numeric) Client's deposit\n"
            "  \"serviceNodeDeposit\": x.xxx,       (numeric) Service node's deposit\n"
            "  \"clientBalance\": x.xxx,            (numeric) Client's current balance\n"
            "  \"serviceNodeBalance\": x.xxx,       (numeric) Service node's balance\n"
            "  \"nonce\": xxx,                      (numeric) Current state nonce\n"
            "  \"challengePeriod\": xxx,            (numeric) Challenge period (seconds)\n"
            "  \"openTime\": xxx,                   (numeric) Channel open timestamp\n"
            "  \"closingTime\": xxx,                (numeric) Closing initiation time\n"
            "  \"state\": \"xxx\",                  (string) Channel state (open/challenged/closed)\n"
            "  \"capacity\": x.xxx,                 (numeric) Total channel capacity\n"
            "  \"availableBalance\": x.xxx          (numeric) Available for payments\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrGetPaymentChannel", "\"abc123...\"")
            + HelpExampleRpc("xrGetPaymentChannel", "\"abc123...\"")
        );

    uint256 channelId;
    channelId.SetHex(request.params[0].get_str());

    auto channel = xrouter::g_paymentChannelManager.GetChannel(channelId);
    if (!channel) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Channel not found");
    }

    return xrouter::ChannelToJSON(*channel);
}

/**
 * RPC: xrListPaymentChannels
 *
 * List all payment channels
 */
static UniValue xrListPaymentChannels(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "xrListPaymentChannels ( \"serviceNodeAddress\" )\n"
            "\nList all payment channels, optionally filtered by service node.\n"
            "\nArguments:\n"
            "1. serviceNodeAddress    (string, optional) Filter by service node address\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"channelId\": \"xxx\",\n"
            "    \"serviceNodeAddress\": \"xxx\",\n"
            "    \"state\": \"xxx\",\n"
            "    \"clientBalance\": x.xxx,\n"
            "    ...\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("xrListPaymentChannels", "")
            + HelpExampleCli("xrListPaymentChannels", "\"0x1234...\"")
            + HelpExampleRpc("xrListPaymentChannels", "")
        );

    std::vector<uint256> channelIds;

    if (request.params.size() > 0) {
        std::string serviceNodeAddress = request.params[0].get_str();
        channelIds = xrouter::g_paymentChannelManager.GetChannelsByServiceNode(serviceNodeAddress);
    } else {
        channelIds = xrouter::g_paymentChannelManager.GetOpenChannels();
    }

    UniValue result(UniValue::VARR);

    for (const auto& channelId : channelIds) {
        auto channel = xrouter::g_paymentChannelManager.GetChannel(channelId);
        if (channel) {
            result.push_back(xrouter::ChannelToJSON(*channel));
        }
    }

    return result;
}

/**
 * RPC: xrCreateChannelPayment
 *
 * Create an off-chain payment within a channel
 */
static UniValue xrCreateChannelPayment(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "xrCreateChannelPayment \"channelId\" amount\n"
            "\nCreate an off-chain payment within an existing payment channel.\n"
            "\nArguments:\n"
            "1. channelId    (string, required) Channel identifier\n"
            "2. amount       (numeric, required) Amount to pay in BLOCK\n"
            "\nResult:\n"
            "{\n"
            "  \"channelId\": \"xxx\",              (string) Channel identifier\n"
            "  \"nonce\": xxx,                      (numeric) New state nonce\n"
            "  \"clientBalance\": x.xxx,            (numeric) New client balance\n"
            "  \"serviceNodeBalance\": x.xxx,       (numeric) New service node balance\n"
            "  \"clientSignature\": \"xxx\",        (string) Client's signature\n"
            "  \"timestamp\": xxx,                  (numeric) State timestamp\n"
            "  \"hash\": \"xxx\"                    (string) State hash\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrCreateChannelPayment", "\"abc123...\" 0.01")
            + HelpExampleRpc("xrCreateChannelPayment", "\"abc123...\", 0.01")
        );

    uint256 channelId;
    channelId.SetHex(request.params[0].get_str());

    CAmount amount = AmountFromValue(request.params[1]);

    xrouter::ChannelStateUpdate stateUpdate;
    if (!xrouter::g_paymentChannelManager.CreatePayment(channelId, amount, stateUpdate)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to create payment");
    }

    return xrouter::StateUpdateToJSON(stateUpdate);
}

/**
 * RPC: xrClosePaymentChannel
 *
 * Close a payment channel (cooperative or challenge)
 */
static UniValue xrClosePaymentChannel(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "xrClosePaymentChannel \"channelId\" ( cooperative )\n"
            "\nClose a payment channel.\n"
            "\nArguments:\n"
            "1. channelId        (string, required) Channel identifier\n"
            "2. cooperative      (boolean, optional, default=false) Use cooperative close if true\n"
            "\nResult:\n"
            "{\n"
            "  \"channelId\": \"xxx\",      (string) Channel identifier\n"
            "  \"closeType\": \"xxx\",      (string) Type of close (cooperative/challenge)\n"
            "  \"txHash\": \"xxx\"          (string) Transaction hash\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrClosePaymentChannel", "\"abc123...\" true")
            + HelpExampleCli("xrClosePaymentChannel", "\"abc123...\"")
            + HelpExampleRpc("xrClosePaymentChannel", "\"abc123...\", false")
        );

    uint256 channelId;
    channelId.SetHex(request.params[0].get_str());

    bool cooperative = request.params.size() > 1 ? request.params[1].get_bool() : false;

    bool success;
    std::string closeType;

    if (cooperative) {
        // For cooperative close, need final state signed by both parties
        // This is simplified - in practice, would need to exchange signatures
        auto channel = xrouter::g_paymentChannelManager.GetChannel(channelId);
        if (!channel) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Channel not found");
        }

        success = xrouter::g_paymentChannelManager.CooperativeClose(channelId, channel->latestState);
        closeType = "cooperative";
    } else {
        success = xrouter::g_paymentChannelManager.ChallengeClose(channelId);
        closeType = "challenge";
    }

    if (!success) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to close channel");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("channelId", channelId.GetHex());
    result.pushKV("closeType", closeType);

    return result;
}

/**
 * RPC: xrFinalizeChannelClose
 *
 * Finalize a challenged channel close
 */
static UniValue xrFinalizeChannelClose(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "xrFinalizeChannelClose \"channelId\"\n"
            "\nFinalize a challenged channel close after the challenge period expires.\n"
            "\nArguments:\n"
            "1. channelId    (string, required) Channel identifier\n"
            "\nResult:\n"
            "{\n"
            "  \"channelId\": \"xxx\",          (string) Channel identifier\n"
            "  \"finalized\": true,             (boolean) Always true if successful\n"
            "  \"clientFinalBalance\": x.xxx,   (numeric) Client's final balance\n"
            "  \"serviceNodeFinalBalance\": x.xxx (numeric) Service node's final balance\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrFinalizeChannelClose", "\"abc123...\"")
            + HelpExampleRpc("xrFinalizeChannelClose", "\"abc123...\"")
        );

    uint256 channelId;
    channelId.SetHex(request.params[0].get_str());

    auto channel = xrouter::g_paymentChannelManager.GetChannel(channelId);
    if (!channel) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Channel not found");
    }

    if (!xrouter::g_paymentChannelManager.FinalizeClose(channelId)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to finalize channel close");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("channelId", channelId.GetHex());
    result.pushKV("finalized", true);
    result.pushKV("clientFinalBalance", ValueFromAmount(channel->clientBalance));
    result.pushKV("serviceNodeFinalBalance", ValueFromAmount(channel->serviceNodeBalance));

    return result;
}

/**
 * RPC: xrGetChannelBalance
 *
 * Get available balance across all channels
 */
static UniValue xrGetChannelBalance(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "xrGetChannelBalance\n"
            "\nGet total available balance across all open payment channels.\n"
            "\nResult:\n"
            "{\n"
            "  \"totalBalance\": x.xxx,     (numeric) Total available balance\n"
            "  \"channelCount\": xxx        (numeric) Number of open channels\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("xrGetChannelBalance", "")
            + HelpExampleRpc("xrGetChannelBalance", "")
        );

    CAmount totalBalance = xrouter::g_paymentChannelManager.GetTotalAvailableBalance();
    std::vector<uint256> openChannels = xrouter::g_paymentChannelManager.GetOpenChannels();

    UniValue result(UniValue::VOBJ);
    result.pushKV("totalBalance", ValueFromAmount(totalBalance));
    result.pushKV("channelCount", static_cast<uint64_t>(openChannels.size()));

    return result;
}

// Register RPC commands
static const CRPCCommand commands[] = {
    { "xrouter", "xrOpenPaymentChannel",     &xrOpenPaymentChannel,     {"serviceNodeAddress", "amount", "challengePeriod"} },
    { "xrouter", "xrGetPaymentChannel",      &xrGetPaymentChannel,      {"channelId"} },
    { "xrouter", "xrListPaymentChannels",    &xrListPaymentChannels,    {"serviceNodeAddress"} },
    { "xrouter", "xrCreateChannelPayment",   &xrCreateChannelPayment,   {"channelId", "amount"} },
    { "xrouter", "xrClosePaymentChannel",    &xrClosePaymentChannel,    {"channelId", "cooperative"} },
    { "xrouter", "xrFinalizeChannelClose",   &xrFinalizeChannelClose,   {"channelId"} },
    { "xrouter", "xrGetChannelBalance",      &xrGetChannelBalance,      {} },
};

void RegisterPaymentChannelRPCCommands(CRPCTable &t)
{
    for (const auto& command : commands) {
        t.appendCommand(command.name, &command);
    }
}
