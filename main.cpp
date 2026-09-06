#ifndef ORDER_BOOK_IMPL
#define ORDER_BOOK_IMPL "OrderBook.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include ORDER_BOOK_IMPL

#if defined(__unix__) || defined(__APPLE__)
    #include <unistd.h>
    #define OMR_HAS_ISATTY 1
#endif

using namespace orderbook;

// ----------------------------------------------------------------- utils

static const char* to_string(Type t) noexcept {
    switch (t) {
        case Type::Limit:  return "limit";
        case Type::Market: return "market";
        case Type::Peg:    return "peg";
    }
    return "?";
}

static const char* to_string(Side s) noexcept {
    return (s == Side::Buy) ? "buy" : "sell";
}

static const char* to_string(Status s) noexcept {
    switch (s) {
        case Status::Active:   return "active";
        case Status::Partial:  return "partial";
        case Status::Total:    return "total";
        case Status::Cancelled: return "cancelled";
    }
    return "?";
}

/* Format id to output. */
static std::string format_id(int id) {
    return "id_" + std::to_string(id);
}

/* Reads a non-negative integer.
 * It rejects trash to the right. */
static std::optional<int> parse_int(const std::string& s) {
    if (s.empty())
        return std::nullopt;
    long long v = 0;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return std::nullopt;
        v = v * 10 + (c - '0');
        if (v > 2'000'000'000LL)
            return std::nullopt;
    }
    return static_cast<int>(v);
}

/* Reads the id from format id_<id>. */
static std::optional<int> parse_id(const std::string& s) {
    return parse_int(s.rfind("id_", 0) == 0 ? s.substr(3) : s);
}

/* Reads a non-negative float and convert it (up
 * to 2 decimal places) to an integer (ticks) */
static std::optional<int> parse_price(const std::string& s) {
    const std::size_t dot = s.find('.');
    if (dot == std::string::npos) {
        auto whole = parse_int(s);
        if (!whole || *whole > 20'000'000)
            return std::nullopt;
        return *whole * 100;
    }
    const std::string int_part  = s.substr(0, dot);
    std::string frac_part = s.substr(dot + 1);
    if (int_part.empty() || frac_part.empty() || frac_part.size() > 2)
        return std::nullopt;
    if (frac_part.size() == 1)
        frac_part += '0';

    auto whole = parse_int(int_part);
    auto frac  = parse_int(frac_part);
    if (!whole || !frac || *whole > 20'000'000)
        return std::nullopt;
    return *whole * 100 + *frac;
}

/* Converts from ticks to float. */
static std::string format_price(int ticks) {
    const int whole = ticks / 100;
    const int frac  = ticks % 100;
    std::ostringstream ss;
    ss << whole;
    if (frac != 0) {
        ss << '.' << char('0' + frac / 10);
        if (frac % 10 != 0)
            ss << char('0' + frac % 10);
    }
    return ss.str();
}

// ----------------------------------------------------------------- CLI

class Cli {
    public:
        Cli(OrderBook& book, std::ostream& out) : book_(book), out_(out) {
            buy_view_.reserve(64);
            sell_view_.reserve(64);
        }

        void run(std::istream& in, bool interactive);

    private:
        void dispatch(const std::vector<std::string>& tok);

        void cmd_new_order(const std::vector<std::string>& tok);
        void cmd_cancel(const std::vector<std::string>& tok);
        void cmd_change(const std::vector<std::string>& tok);
        void cmd_consult(const std::vector<std::string>& tok);
        void cmd_print_book();
        void cmd_help();

        void flush_trades();
        void reject(Reject r) { out_ << "Rejected: " << to_string(r) << '\n'; }
        void usage(const char* what) { out_ << "Usage: " << what << '\n'; }

        static std::string cell(const OrderBook::BookOut& e);
        static void fix_peg_prices(std::vector<OrderBook::BookOut>& v);

        OrderBook& book_;
        std::ostream& out_;
        bool running_ = true;
        std::vector<OrderBook::BookOut> buy_view_, sell_view_;
};

void Cli::flush_trades() {
    const auto& trades = book_.pending_trades();

    for (std::size_t i = 0, j; i < trades.size();) {
        const int price = trades[i].price;
        int qty = 0;
        j = i;
        const std::string price_str = format_price(price);
        while (i < trades.size() && trades[i].price == price)
            qty += trades[i++].qty;
        out_ << "Trade - price: " << price_str << ", qty: " << qty << '\n';
        for (; j < i; j++)
            out_ << '\t' << "buy order " << format_id(trades[j].bid_id)
                << ", sell order " << format_id(trades[j].offer_id)
                << " - price: " << price_str << ", qty: " << trades[j].qty << '\n';

    }
    book_.clear_trades();
}

void Cli::cmd_new_order(const std::vector<std::string>& tok) {
    // tok[0] = limit | market | peg
    const Type type = (tok[0] == "limit")  ? Type::Limit
                    : (tok[0] == "market") ? Type::Market
                                           : Type::Peg;

    std::size_t i = 1;

    // "peg bid buy" / "peg offer sell" / "peg buy" / "peg sell"
    std::optional<std::string> peg_ref;
    if (type == Type::Peg && i < tok.size() && (tok[i] == "bid" || tok[i] == "offer"))
        peg_ref = tok[i++];

    if (i >= tok.size())
        return usage("<type> <side> <qty> [price]");
    Side side;
    if (tok[i] == "buy")
        side = Side::Buy;
    else if (tok[i] == "sell")
        side = Side::Sell;
    else
        return usage("<type> buy|sell <qty> [price]");
    ++i;

    // A *peg to the bid* will be considered to the buy side, as also a
    // *peg to the offer* to the sell side.
    if (peg_ref) {
        const bool coherent = (*peg_ref == "bid"   && side == Side::Buy)
                           || (*peg_ref == "offer" && side == Side::Sell);
        if (!coherent) {
            out_ << "Rejected: peg " << *peg_ref << ' ' << to_string(side)
                 << " is not supported (a peg to the bid follows the buy side, "
                    "a peg to the offer follows the sell side)\n";
            return;
        }
    }

    const std::size_t expected = (type == Type::Limit) ? i + 2 : i + 1;
    if (tok.size() != expected)
        return usage(type == Type::Limit ? "limit buy|sell <qty> <price>"
                                         : "market|peg buy|sell <qty>");

    // Ordem dos argumentos: <type> <side> <qty> [price].
    const std::optional<int> qty = parse_int(tok[i]);
    if (!qty || *qty <= 0)
        return usage("qty must be a positive integer");

    std::optional<int> price;
    if (type == Type::Limit) {
        price = parse_price(tok[i + 1]);
        if (!price)
            return usage("price must be a number with up to 2 decimals");
    }

    const OrderBook::OpResult r = book_.insert_order(type, side, *qty, price);
    if (!r.check()) {
        // A market order with no counterparty returns NoMarketPrice. But it has
        // an id and it is recorded in the history as cancelled.
        reject(r.reject);
        if (r.id >= 0)
            out_ << "Order " << format_id(r.id) << " cancelled.\n";
        return;
    }

    out_ << "Order created: " << format_id(r.id) << ' ' << to_string(type) << ' '
         << to_string(side) << ' ' << *qty;
    if (type == Type::Limit)
        out_ << " @ " << format_price(*price);
    else if (type == Type::Peg)
        out_ << " @ " << (side == Side::Buy ? "bid" : "offer");
    out_ << '\n';

    flush_trades();
}

void Cli::cmd_cancel(const std::vector<std::string>& tok) {
    // "cancel order <id>" ou "cancel <id>"
    const std::size_t i = (tok.size() >= 2 && tok[1] == "order") ? 2 : 1;
    if (tok.size() != i + 1)
        return usage("cancel order <id>");

    const std::optional<int> id = parse_id(tok[i]);
    if (!id)
        return usage("cancel order <id>");

    const OrderBook::OpResult r = book_.cancel_order(*id);
    if (!r.check())
        return reject(r.reject);
    out_ << "Order " << format_id(*id) << " cancelled.\n";
}

void Cli::cmd_change(const std::vector<std::string>& tok) {
    // "change order <id> <qty> [price]"
    const std::size_t i = (tok.size() >= 2 && tok[1] == "order") ? 2 : 1;
    if (tok.size() != i + 2 && tok.size() != i + 3)
        return usage("change order <id> <qty> [price]");

    const std::optional<int> id  = parse_id(tok[i]);
    const std::optional<int> qty = parse_int(tok[i + 1]);
    if (!id || !qty || *qty <= 0)
        return usage("change order <id> <qty> [price]");

    std::optional<int> price;
    if (tok.size() == i + 3) {
        price = parse_price(tok[i + 2]);
        if (!price)
            return usage("price must be a number with up to 2 decimals");
    }

    const OrderBook::OpResult r = book_.change_order(*id, *qty, price);
    if (!r.check())
        return reject(r.reject);

    out_ << "Order " << format_id(*id) << " changed: qty " << *qty;
    if (price)
        out_ << " @ " << format_price(*price);
    out_ << '\n';

    flush_trades();
}

void Cli::cmd_consult(const std::vector<std::string>& tok) {
    // "consult order <id>" ou "consult <id>"
    const std::size_t i = (tok.size() >= 2 && tok[1] == "order") ? 2 : 1;
    if (tok.size() != i + 1)
        return usage("consult order <id>");

    const std::optional<int> id = parse_id(tok[i]);
    if (!id)
        return usage("consult order <id>");

    const Order* order = book_.locate_order(*id);
    if (order == nullptr)
        return reject(Reject::NotFound);

    out_ << "Order " << format_id(order->get_id()) << ": " << to_string(order->get_type())
         << ' ' << to_string(order->get_side()) << ' ' << order->get_qty() << " @ "
         << (order->get_type() == Type::Limit ? format_price(order->get_price())
                                          : std::string("-"))
         << ' ' << to_string(order->get_status()) << '\n';
}

std::string Cli::cell(const OrderBook::BookOut& e) {
    std::ostringstream ss;
    ss << format_id(e.id) << ' ' << to_string(e.type) << ' ' << e.qty << " @ "
       << (e.price > 0 ? format_price(e.price) : std::string("-"))
       << ' ' << to_string(e.status);
    return ss.str();
}

void Cli::cmd_print_book() {
    book_.book_view(Side::Buy, buy_view_);
    book_.book_view(Side::Sell, sell_view_);

    std::vector<std::string> left, right;
    left.reserve(buy_view_.size());
    right.reserve(sell_view_.size());
    for (const auto& e : buy_view_)
        left.push_back(cell(e));
    for (const auto& e : sell_view_)
        right.push_back(cell(e));

    std::size_t width = 3; // len("Buy")
    for (const std::string& s : left)
        width = std::max(width, s.size());
    width += 1;

    auto pad = [&](std::string s) { s.resize(width, ' '); return s; };

    out_ << pad("Buy") << "| Sell\n"
         << std::string(width, '-') << '|' << std::string(width, '-') << '\n';

    const std::size_t rows = std::max(left.size(), right.size());
    if (rows == 0) {
        out_ << pad("") << "|\n";
        return;
    }
    for (std::size_t i = 0; i < rows; ++i) {
        out_ << pad(i < left.size() ? left[i] : std::string{}) << "| "
             << (i < right.size() ? right[i] : std::string{}) << '\n';
    }
}

void Cli::cmd_help() {
    out_ << "Commands:\n"
            "  limit  buy|sell <qty> <price>       e.g. limit buy 100 10\n"
            "  market buy|sell <qty>               e.g. market buy 150\n"
            "  peg    [bid|offer] buy|sell <qty>   e.g. peg bid buy 150\n"
            "  cancel  order <id>                  e.g. cancel order id_0\n"
            "  change  order <id> <qty> [price]\n"
            "  consult order <id>\n"
            "  print book\n"
            "  help | quit\n";
}

void Cli::dispatch(const std::vector<std::string>& tok) {
    const std::string& head = tok[0];

    if (head == "limit" || head == "market" || head == "peg")
        cmd_new_order(tok);
    else if (head == "cancel")
        cmd_cancel(tok);
    else if (head == "change")
        cmd_change(tok);
    else if (head == "consult")
        cmd_consult(tok);
    else if (head == "print" || head == "book")
        cmd_print_book();
    else if (head == "help")
        cmd_help();
    else if (head == "quit" || head == "exit")
        running_ = false;
    else out_ << "Unknown command: " << head << " (try \"help\")\n";
}

void Cli::run(std::istream& in, bool interactive) {
    std::string line;
    while (running_) {
        if (interactive) {
            out_ << ">>> ";
            out_.flush();
        }
        if (!std::getline(in, line))
            break;

        // tokeniza e normaliza para minusculas (ids e numeros nao mudam)
        std::vector<std::string> tok;
        std::istringstream ss(line);
        for (std::string w; ss >> w;) {
            std::transform(w.begin(), w.end(), w.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            tok.push_back(std::move(w));
        }
        if (tok.empty() || tok[0][0] == '#')
            continue; // vazio ou comentario

        dispatch(tok);
    }
}

// ----------------------------------------------------------------- main

int main() {
    std::ios::sync_with_stdio(false);

    bool interactive = true;
#ifdef OMR_HAS_ISATTY
    interactive = isatty(fileno(stdin)) != 0;
#endif

    OrderBook book;
    Cli cli(book, std::cout);

    if (interactive) {
        std::cout << "Order Matching Engine - 1 asset, FIFO price/time.\n"
                     "Type \"help\" for the command list, \"quit\" to exit.\n";
    }
    cli.run(std::cin, interactive);
    return 0;
}
