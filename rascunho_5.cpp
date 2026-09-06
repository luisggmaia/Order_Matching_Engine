#include <list>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <optional>
#include "OrderedMap.hpp"


// enum class Type

enum class Type : char { Limit, Market, Peg };

// enum class Side

enum class Side : bool { Sell, Buy };

static_assert(static_cast<std::size_t>(Side::Sell) == 0);
static_assert(static_cast<std::size_t>(Side::Buy)  == 1);

static constexpr std::size_t idx(Side s) noexcept {
    return static_cast<std::size_t>(s);
}
static constexpr Side opposite(Side s) noexcept {
    return static_cast<Side>(idx(s) ^1u);
}

// enum class Status

enum class Status : char { Active, Partial, Total, Canceled };

// struct Order

struct Order {
    Order(Type t, Side s, int q, std::optional<int> p) {
        type = t; side = s; qty = q;
        if (p.has_value())
            price = p;
    }

    Type type;
    Side side;
    int qty, price;

    int get_id() const noexcept { return id; }
    Status get_status() const noexcept { return status; }

    private:
        int id;
        int seq;
        Status status;

        friend class OrderBook;
};

struct Trade {
    int qty, price;
    int bid_order_id, offer_order_id;
    Side agressor;
};

// class OrderBook

class OrderBook {
    private:
        using OrderList = std::list<Order>;
        using BookMap = OrderedMap<int, OrderList>;
        using IndexMap = std::unordered_map<int, OrderList::iterator>;

        // The index order of map_book must be kept according to Side struct: Side::Sell = 0, Side::Buy = 1.
        // Although both BookMaps are equal in type, the buy map will be ordered in decreasing order by
        // rewriting its index with opposite (minus) sign. The order price will keep the same.
        BookMap map_book[2];
        OrderList peg_book[2];
        OrderList history;
        IndexMap ids;

        int time = 1; // order limit_book time.
        int id_count = 0; // order id counter.

        BookMap& limit_map(Side s) noexcept { return map_book[idx(s)]; }
        OrderList& peg_list(Side s) noexcept { return peg_book[idx(s)]; }
        int order_key(Order& order) noexcept { return (order.side == Side::Buy) ? -order.price : order.price; }

        void run_matching();
        void match(Order& bid_order, Order& offer_order);
        bool set_match_order(Side s) noexcept;

    public:
        int insert_order(Order order);
        bool cancel_order(int order_id);
        bool change_order(int order_id, int qty, std::optional<int> opt_price);

        const std::vector<Trade>& last_trades() const noexcept { return trades_; }

        void print_book();

    private:
        // Buffers
        struct SideState {
            BookMap::iterator book_it;
            bool is_peg;
            OrderList* list_ptr;
            Order* top_order_ptr;
        };
        SideState side_[2];

        SideState& state(Side s) noexcept { return side_[idx(s)]; }
        const SideState& state(Side s) const noexcept { return side_[idx(s)]; }

        std::vector<Trade> trades_;
};

int OrderBook::insert_order(Order order) {
    order.seq = time++;
    order.id = id_count++;
    order.status = Status::Active;
    switch (order.type) {
        case Type::Peg: {
            // time: O(1); memory: O(1).
            // Insert the order to the back of the (side) peg_list book.
            peg_list(order.side).push_back(order);
            // Create the index.
            ids[order.id] = std::prev(peg_list(order.side).end());
            break;
        }
        case Type::Limit: {
            // time: O(1), if the key already exists, O(log n) otherwise; memory: O(1).
            // Inserts inline the price in the (order.side) OrderMap. Returns the iterator for the created list or for the existing.
            // Important to note the sign inversion for the Buy side, in order to maintain a decreasing order.
            auto it_map = limit_map(order.side).try_emplace(order_key(order)).first;
            // time: O(1); memory: O(1)
            // Insert the order to the back of the (side) limit book.
            it_map->second.push_back(order);
            // Create the index.
            ids[order.id] = std::prev(it_map->second.end());

            run_matching();
            break;
        }
        case Type::Market: {
            // The market order demands the opposite side book to be not empty - cause its price is based on the best price of the other side.
            if (!set_match_order(opposite(order.side))) {
                std::cerr << "Error: no market price. Order canceled.\n";
                order.status = Status::Canceled;
            }
            else {
                // Set the order price as the best opposite side price.
                // Important to note the minus sign. Because the price is taken from the opposite map index.
                order.price = (order.side == Side::Buy) ? limit_map(Side::Sell).begin()->first : -limit_map(Side::Buy).begin()->first;

                if (order.side == Side::Buy)
                    do
                        match(order, *state(Side::Sell).top_order_ptr);
                    while (order.qty > 0 && set_match_order(Side::Sell));
                else
                    do
                        match(*state(Side::Buy).top_order_ptr, order);
                    while (order.qty > 0 && set_match_order(Side::Buy));
                
                order.status = (order.qty > 0) ? Status::Partial : Status::Total;
            }

            // time: O(1); memory: O(1).
            history.push_front(order);
            // Create the index;
            ids[order.id] = history.begin();
            break;
        }
    }

    return order.id;
}

bool OrderBook::cancel_order(int order_id) {
    auto it_id = ids.find(order_id);
    if (it_id == ids.end()) {
        std::cerr << "Error: order not found.\n";
        return false;
    }
    OrderList::iterator it = it_id->second;
    if (it->status == Status::Canceled) {
        std::cerr << "Error: order already canceled.\n";
        return false;
    }
    else if (it->status == Status::Total) {
        std::cerr << "Error: order already totally executed.\n";
        return false;
    }
    else if (it->type == Type::Market) {
        std::cerr << "Error: market order already canceled.\n";
        return false;
    }

    if (it->type == Type::Peg)
        // time: O(1); memory: O(1).
        // Removes the order from the (side) peg_list book and inserts it in the history.
        history.splice(history.begin(), peg_list(it->side), it);
    else {
        // time: O(1); memory: O(1).
        // O(1) time complexity due to OrderedMap structure.
        // Iterator for an map limit_map(it->side) element (list).
        // Important to note the minus sign. Because the price is taken from the opposite map index.
        auto it_map = limit_map(it->side).find(order_key(*it));

        // time: O(1); memory: O(1).
        history.splice(history.begin(), it_map->second, it);
        if (it_map->second.empty())
            // Remove the map node. Needed to keep easy location of bid and ask.
            limit_map(it->side).erase(it_map->first);
    }
    it->status = Status::Canceled;

    return true;
}

bool OrderBook::change_order(int order_id, int qty, std::optional<int> opt_price) {
    auto it_id = ids.find(order_id);
    if (it_id == ids.end()) {
        std::cerr << "Error: order not found.\n";
        return false;
    }
    OrderList::iterator it = it_id->second;
    if (it->status == Status::Canceled) {
        std::cerr << "Error: order canceled.\n";
        return false;
    }
    else if (it->status == Status::Total) {
        std::cerr << "Error: order totally executed.\n";
        return false;
    }
    else if (it->type == Type::Market) {
        std::cerr << "Error: market order already canceled.\n";
        return false;
    }

    if (qty > it->qty || opt_price.has_value())
        it->seq = time++;
    it->qty = qty;
    if (it->type == Type::Peg) {
        if (it->seq == time - 1)
            // time: O(1); memory: O(1)
            // Moves the order to the back of the (side) peg_list book.
            peg_list(it->side).splice(peg_list(it->side).end(), peg_list(it->side), it);
    }
    else if (it->seq == time - 1 || opt_price.has_value()) {
        // time: O(1); memory: O(1).
        // O(1) time complexity due to OrderedMap structure
        // Iterator for an map limit_map(it->side) element (list).
        auto it_map = limit_map(it->side).find(order_key(*it));

        auto new_it_map = it_map;
        if (opt_price.has_value() && opt_price.value() != it->price) {
            it->price = opt_price.value();
            // time: O(1), if the key already exists, O(log n) otherwise; memory: O(1).
            // Gets the other price list.
            new_it_map = limit_map(it->side).try_emplace((it->side == Side::Buy) ? -opt_price.value() : opt_price.value()).first;
        }
        // Moves the order to the back.
        new_it_map->second.splice(new_it_map->second.end(), it_map->second, it);
        if (it_map->second.empty())
            // If the list empties, the map node must be deleted.
            limit_map(it->side).erase(it_map->first);

        run_matching();
    }

    return true;
}

bool OrderBook::set_match_order(Side s) noexcept {
    auto& book = limit_map(s);
    if (book.empty())
        return false;
    auto& st = state(s);
    auto& pegs = peg_list(s);

    st.book_it = book.begin();
    // Evaluate whether the first pegged order has priority to the first limit order (according to the time/id)
    st.is_peg = !pegs.empty() && pegs.front().seq < st.book_it->second.front().seq;
    // Chooses the priority according to the time
    if (st.is_peg) {
        st.top_order_ptr = &pegs.front();
        // st.top_order_ptr->price = st.book_it->second.front().price;
        st.list_ptr = &pegs;
    }
    else {
        st.top_order_ptr = &st.book_it->second.front();
        st.list_ptr = &st.book_it->second;
    }
    // st.top_order_ptr = st.is_peg ? &pegs.front() : &st.book_it->second.front();
    return true;
}

void OrderBook::run_matching() {
    while (set_match_order(Side::Buy) && set_match_order(Side::Sell)) {
        if (-state(Side::Buy).book_it->first < state(Side::Sell).book_it->first)
            // Important to note the minus sign on the Buy side.
            // There is no trade when bid < offer
            return;

        match(*state(Side::Buy).top_order_ptr, *state(Side::Sell).top_order_ptr);
    }
}

void OrderBook::match(Order& bid_order, Order& offer_order) {
    Trade trade {};
    trade.bid_order_id = bid_order.id;
    trade.offer_order_id = offer_order.id;
    // The executed price is determined by the prioritary order.
    // Important to note the minus sign on the Buy side.
    trade.price = (bid_order.seq < offer_order.seq) ? -state(Side::Buy).book_it->first : state(Side::Sell).book_it->first;

    // Evaluate whether the buy qty is smaller, greater than or equal to the sell qty
    if (bid_order.qty < offer_order.qty) {
        // Bid's qty is smaller than offer's. Its trade is uncomplete.
        // Updates the qty for the offer order.
        offer_order.qty -= bid_order.qty;
        bid_order.status = Status::Total;
        offer_order.status = Status::Partial;
        trade.qty = bid_order.qty;
        trade.agressor = Side::Sell;
        // Requires c++20.
        // If the order is inserted in the book, then moves it to the history.
        auto h = ids.find(bid_order.id);
        if (h != ids.end()) {
            // If this order is in the book, it is certainly in the state(Side::Buy).book_it (best bid list).
            history.splice(history.begin(), *state(Side::Buy).list_ptr, h->second);
            if (!state(Side::Buy).is_peg && state(Side::Buy).list_ptr->empty())
                // If the list empties
                limit_map(Side::Buy).erase(state(Side::Buy).book_it->first);
        }
    }
    else if (bid_order.qty > offer_order.qty) {
        // Offer's qty is smaller than bid's. Its trade is uncomplete.
        // Updates the qty for the bid order.
        bid_order.qty -= offer_order.qty;
        bid_order.status = Status::Partial;
        offer_order.status = Status::Total;
        trade.qty = offer_order.qty;
        trade.agressor = Side::Buy;
        // Requires c++20.
        // If the order is inserted in the book, then moves it to the history.
        auto h = ids.find(offer_order.id);
        if (h != ids.end()) {
            // If this order is in the book, it is certainly in the state(Side::Sell).book_it (best offer list).
            history.splice(history.begin(), *state(Side::Sell).list_ptr, h->second);
            if (!state(Side::Sell).is_peg && state(Side::Sell).list_ptr->empty())
                // If the list empties
                limit_map(Side::Sell).erase(state(Side::Sell).book_it->first);
        }
    }
    else {
        trade.qty = bid_order.qty; // = offer_order.qty;
        trade.agressor = Side::Buy;
        bid_order.status = Status::Total;
        offer_order.status = Status::Total;
        auto h = ids.find(bid_order.id);
        if (h != ids.end()) {
            history.splice(history.begin(), *state(Side::Buy).list_ptr, h->second);
            if (!state(Side::Buy).is_peg && state(Side::Buy).list_ptr->empty())
                limit_map(Side::Buy).erase(state(Side::Buy).book_it->first);
        }
        h = ids.find(offer_order.id);
        if (h != ids.end()) {
            history.splice(history.begin(), *state(Side::Sell).list_ptr, h->second);
            if (!state(Side::Sell).is_peg && state(Side::Sell).list_ptr->empty())
                limit_map(Side::Sell).erase(state(Side::Sell).book_it->first);
        }
    }

    trades_.push_back(trade);
}

void OrderBook::print_book() {

}


