#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <list>
#include <optional>
#include <vector>

#include "OrderedMap.hpp"

namespace orderbook {

// ---------------------------------------------------------------- enums ----

enum class Type : char { Limit, Market, Peg };

enum class Side : bool { Sell, Buy };

static_assert(static_cast<std::size_t>(Side::Sell) == 0);
static_assert(static_cast<std::size_t>(Side::Buy) == 1);

enum class Status : char { Active, Partial, Total, Cancelled };

enum class Reject : char {
    None,
    NotFound,
    AlreadyCancelled,
    AlreadyFilled,
    NotResting,
    NoMarketPrice,
    MissingPrice,
    UnexpectedPrice,
    InvalidQty
};

inline constexpr const char* to_string(Reject r) noexcept {
    switch (r) {
        case Reject::None:            return "ok";
        case Reject::NotFound:        return "order not found";
        case Reject::AlreadyCancelled: return "order already cancelled";
        case Reject::AlreadyFilled:   return "order already totally executed";
        case Reject::NotResting:      return "market order does not rest in the book";
        case Reject::NoMarketPrice:   return "no market price - insert a limit order first";
        case Reject::MissingPrice:    return "limit order requires a price";
        case Reject::UnexpectedPrice: return "market/peg order must not carry a price";
        case Reject::InvalidQty:      return "quantity must be positive";
    }
    return "unknown";
}

inline constexpr std::size_t idx(Side s) noexcept {
    return static_cast<std::size_t>(s);
}
inline constexpr Side opposite(Side s) noexcept {
    return static_cast<Side>(idx(s) ^ 1u);
}

// ---------------------------------------------------------------- Order ----

// Grouped fields due to alignment (memory efficiency).
// 20 Bytes.
struct Order {
    public:
        int    get_id()     const noexcept { return id; }
        int    get_qty()    const noexcept { return qty; }
        int    get_price()  const noexcept { return price; }
        Type   get_type()   const noexcept { return type; }
        Side   get_side()   const noexcept { return side; }
        Status get_status() const noexcept { return status; }

    private:
        Order(Type t, Side s, int q, int p) noexcept : 
            qty(q), price(p), id(0), seq(0),
            type(t), side(s), status(Status::Active) {}

        int qty, price, id, seq;
        Type type;
        Side side;
        Status status;

        friend class OrderBook;
};

// ---------------------------------------------------------------- Trade ----

// 20 Bytes.
struct Trade {
    int price, qty;
    int bid_id, offer_id;
    Side aggressor; // Aggressor side.
};

// ------------------------------------------------------------ OrderBook ----

class OrderBook {
    public:
        // 8 Bytes.
        struct OpResult {
            OpResult(Reject r, int i = -1) : id(i), reject(r) {}

            int id = -1;
            Reject reject = Reject::None;
            bool check() const noexcept { return reject == Reject::None; }
        };

        // 16 Bytes.
        struct BookOut {
            int id, qty, price;
            Type type;
            Status status;
        };

        explicit OrderBook(std::size_t capacity_hint = 64);

        OrderBook(const OrderBook&)            = delete;
        OrderBook& operator=(const OrderBook&) = delete;
        OrderBook(OrderBook&&)                 = default;
        OrderBook& operator=(OrderBook&&)      = default;

        OpResult insert_order(Type type, Side side, int qty,
                                  std::optional<int> price = std::nullopt);
        OpResult cancel_order(int order_id);
        OpResult change_order(int order_id, int new_qty,
                            std::optional<int> new_price = std::nullopt);
        
        void book_view(Side s, std::vector<BookOut>& view) const;
        const Order* locate_order(int order_id) const;

        const std::vector<Trade>& pending_trades() const noexcept { return trades_; }
        void clear_trades() noexcept { trades_.clear(); }

    private:
        using OrderQueue = std::list<Order>;
        using BookMap = OrderedMap<int, OrderQueue>;

        static constexpr int key_of(Side s, int price) noexcept {
            return (s == Side::Buy) ? -price : price;
        }
        static constexpr int price_of(Side s, int key) noexcept {
            return (s == Side::Buy) ? -key : key;
        }

        // 24 Bytes (in a 64-bit machine; 12 Bytes in a 32-bit).
        struct TopQueue {
            OrderQueue *lim_queue = nullptr;
            int price = 0;
        };

        // 24 Bytes (in a 64-bit machine; 12 Bytes in a 32-bit).
        struct TopOrder {
            OrderQueue* queue = nullptr;
            Order* order = nullptr;
            bool is_peg = false;
        };

        BookMap&          lim_map(Side s)         noexcept { return lim_book_[idx(s)]; }
        const BookMap&    lim_map(Side s)   const noexcept { return lim_book_[idx(s)]; }
        OrderQueue&       peg_queue(Side s)       noexcept { return peg_book_[idx(s)]; }
        const OrderQueue& peg_queue(Side s) const noexcept { return peg_book_[idx(s)]; }
        TopQueue&         top_queue(Side s)       noexcept { return top_queues_[idx(s)]; }
        TopOrder&         top_order(Side s)       noexcept { return top_orders_[idx(s)]; }

        bool resolve_top_queue(Side s) noexcept;
        bool resolve_top_order(Side s) noexcept;
        void execute(Order& bid, Order& offer, int price);
        void retire_top_order(Side s);
        void run_matching();

        // The index order of map_book must be kept according to Side struct: Side::Sell = 0, Side::Buy = 1.
        // Vide static_assert.
        // Although both BookMaps are equal in type, the buy map will be ordered in decreasing order by
        // rewriting its index with opposite (minus) sign. The order price will keep the same.
        BookMap lim_book_[2];
        OrderQueue peg_book_[2];
        OrderQueue history_;
        std::vector<OrderQueue::iterator> ids_;
        TopQueue top_queues_[2];
        TopOrder top_orders_[2];
        std::vector<Trade> trades_;

        int seq_count_ = 0; // book time.
        int id_count_ = 0; // order id counter.
};

}  // namespace orderbook

#endif  // ORDER_BOOK_HPP
