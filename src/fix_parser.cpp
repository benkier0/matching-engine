#include "fix_parser.h"
#include <iomanip>
#include <sstream>
#include <unordered_map>

static constexpr char kSOH = '\x01';

// split the message on SOH and extract every tag=value pair into a map
// tags may appear multiple times (e.g repeating groups) the last value wins
static std::unordered_map<std::string, std::string>
extract_tags(std::string_view raw) {
    std::unordered_map<std::string, std::string> tags;
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eq  = raw.find('=', pos);
        if (eq == std::string_view::npos) break;

        size_t end = raw.find(kSOH, eq + 1);
        if (end == std::string_view::npos) end = raw.size();

        tags.emplace(std::string(raw.substr(pos, eq - pos)),
                     std::string(raw.substr(eq + 1, end - eq - 1)));
        pos = end + 1;
    }
    return tags;
}

std::optional<FIXMessage> FIXParser::parse(std::string_view raw) {
    auto tags = extract_tags(raw);

    auto get = [&](const char* tag) -> std::optional<std::string_view> {
        auto it = tags.find(tag);
        if (it == tags.end()) return std::nullopt;
        return it->second;
    };

    auto msg_type = get("35");
    if (!msg_type) return std::nullopt;

    if (*msg_type == "D") {
        auto cl_ord_id = get("11");
        auto side_str  = get("54");
        auto qty_str   = get("38");
        auto type_str  = get("40");
        if (!cl_ord_id || !side_str || !qty_str || !type_str) return std::nullopt;

        FIXNewOrder msg;
        msg.cl_ord_id  = std::string(*cl_ord_id);
        msg.side       = (*side_str == "1") ? Side::BUY : Side::SELL;
        msg.order_type = (*type_str == "2") ? OrderType::LIMIT : OrderType::MARKET;
        msg.quantity   = std::stoll(std::string(*qty_str));
        msg.price      = 0;

        if (msg.order_type == OrderType::LIMIT) {
            auto price_str = get("44");
            if (!price_str) return std::nullopt;
            // Convert decimal string (e.g. "150.05") to fixed-point cents.
            msg.price = static_cast<int64_t>(std::stod(std::string(*price_str)) * 100.0 + 0.5);
        }

        return msg;
    }

    if (*msg_type == "F") {
        auto cl_ord_id      = get("11");
        auto orig_cl_ord_id = get("41");
        auto side_str       = get("54");
        if (!cl_ord_id || !orig_cl_ord_id || !side_str) return std::nullopt;

        return FIXCancelRequest{
            std::string(*cl_ord_id),
            std::string(*orig_cl_ord_id),
            (*side_str == "1") ? Side::BUY : Side::SELL
        };
    }

    return std::nullopt;
}

std::string FIXParser::build_new_order(const FIXNewOrder& msg) {
    std::ostringstream ss;
    ss << "8=FIX.4.2"    << kSOH
       << "35=D"          << kSOH
       << "11=" << msg.cl_ord_id << kSOH
       << "54=" << (msg.side == Side::BUY ? "1" : "2") << kSOH
       << "40=" << (msg.order_type == OrderType::LIMIT ? "2" : "1") << kSOH
       << "38=" << msg.quantity << kSOH;

    if (msg.order_type == OrderType::LIMIT) {
        ss << "44=" << std::fixed << std::setprecision(2)
           << (msg.price / 100.0) << kSOH;
    }

    return ss.str();
}
