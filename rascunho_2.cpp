#include <list>
#include <map>
#include <unordered_map>
#include <iostream>
#include <stdexcept>


// enum class Type

enum class Type : char { Limit, Market, Peg };

// enum class Side

enum class Side : bool { Buy = 1, Sell = 0 };

// enum class Status

enum class Status : char { Active, Partial, Total, Canceled };

// struct Order

struct Order {
    Order(Type t, Side s, int q, int p = 0)
        : type(t), side(s), qty(q), price(p) {};

    private:
        int id;
        int qty, price;
        Type type;
        Side side;
        Status status;

        friend class OrderBook;
};

struct OrderChange {
    int qty, price = 0;
};

struct Trade {
    int qty, price;
    int bid_order_id, offer_order_id;
};

// class OrderBook

class OrderBook {
    private:
        template <typename T>
        class SideArray {
            private:
                std::array<T, 2> data;

            public:
                T& operator[](Side side) noexcept {
                    return data[static_cast<std::size_t>(side)];
                }

                const T& operator[](Side side) const noexcept {
                    return data[static_cast<std::size_t>(side)];
                }
        };

        using OrderList = std::list<Order>;

        SideArray<std::map<int, OrderList>> limit_book;
        SideArray<OrderList> peg_book;
        OrderList history;
        std::unordered_map<int, OrderList::iterator> ids;
        // list com Key int (time) e Value int (qty)
        // map with Key int (price) e Value list<int, int>>
        int time = 1; // order limit_book time

    public:
        OrderBook();
        bool update_book();
        void print_book();
        OrderList::iterator locate_order(int order_id);
        int insert_order(Order order);
        bool cancel_order(int order_id);
        bool change_order(int order_id, OrderChange& order_change);

        const Trade& get_trade() const { return executed_trade; }

    private:
        // Needed variables for the update function
        std::map<int, OrderList>::iterator bid_it, offer_it;
        bool peg_bid, peg_offer;
        OrderList *bid_ptr, *offer_ptr;

        Trade executed_trade;
    
    public:
        class OrderIterator {
            public:
                using ListIt = OrderList::iterator;
                using MapIt = std::map<int, OrderList>::iterator;

                OrderList::iterator it;

                template <typename Iterator>
                OrderIterator(Side s) : side(s) {
                    auto it_map = (side == Side::Buy) ? limit_book[Side::Buy].begin() : limit_book[Side::Sell].rbegin();
                    it = it_map.begin();
                }

            private:
                ListIt it_aux;
                Side side;
                bool status;
            
        }

};

std::list<Order>::iterator OrderBook::locate_order(int order_id) {
    auto it_id = ids.find(order_id);
    if (it_id == ids.end())
        throw std::domain_error("id not valid");

    return it_id->second;
}

bool OrderBook::update_book() {
    if (limit_book[Side::Buy].empty() or limit_book[Side::Sell].empty())
        return false;

    bid_it = std::prev(limit_book[Side::Buy].end());
    offer_it = limit_book[Side::Sell].begin();
    if (bid_it->first < offer_it->first)
        // There is no trade when bid < offer
        return false;

    // Evaluate whether the first pegged order has priority to the first limit order (according to the time/id)
    peg_bid = !peg_book[Side::Buy].empty() && (peg_book[Side::Buy].front().id < bid_it->second.front().id);
    peg_offer = !peg_book[Side::Sell].empty() && (peg_book[Side::Sell].front().id < offer_it->second.front().id);

    // Chooses the priority according to the time
    bid_ptr = (peg_bid) ? &(peg_book[Side::Buy]) : &(bid_it->second);
    offer_ptr = (peg_offer) ? &(peg_book[Side::Sell]) : &(offer_it->second);

    executed_trade.bid_order_id = bid_ptr->front().id;
    executed_trade.offer_order_id = offer_ptr->front().id;

    // Evaluate whether the buy qty is smaller, greater than or equal to the sell qty
    if (bid_ptr->front().qty < offer_ptr->front().qty) {
        // Bid's qty is smaller than offer's. Its trade is uncomplete.
        // Updates the qty for the offer order.
        offer_ptr->front().qty -= bid_ptr->front().qty;
        executed_trade.qty = bid_ptr->front().qty;
        bid_ptr->pop_front();
        if (!peg_bid && bid_it->second.empty())
            // If the list empties
            limit_book[Side::Buy].erase(bid_it);
    }
    else if (bid_ptr->front().qty > offer_ptr->front().qty) {
        // Offer's qty is smaller than bid's. Its trade is uncomplete.
        // Updates the qty for the bid order.
        bid_ptr->front().qty -= offer_ptr->front().qty;
        executed_trade.qty = offer_ptr->front().qty;
        offer_ptr->pop_front();
        if (!peg_offer && offer_it->second.empty())
            // If the list empties
            limit_book[Side::Sell].erase(offer_it);
    }
    else {
        executed_trade.qty = bid_ptr->front().qty; // = offer_ptr->front().qty;
        bid_ptr->pop_front();
        offer_ptr->pop_front();
        if (!peg_bid && bid_it->second.empty())
            limit_book[Side::Buy].erase(bid_it);
        if (!peg_offer && offer_it->second.empty())
            limit_book[Side::Sell].erase(offer_it);
    }

    // The executed price is determined by the prioritary order.
    executed_trade.price = (executed_trade.bid_order_id < executed_trade.offer_order_id) ? bid_it->first : offer_it->first;

    return true;
}

void OrderBook::print_book() {

}

int OrderBook::insert_order(Order order) {
    order.id = time;
    order.status = Status::Active;
    switch (order.type) {
        case Type::Peg: {
            // time: O(1); memory: O(1)
            peg_book[order.side].push_back(order);
            break;
        }
        case Type::Limit: {
            // time: O(log n); memory: O(1).
            // Inserts inline the price in the (order.side) map. Returns the iterator for the created list or for the existing.
            auto it_map = limit_book[order.side].try_emplace(order.price).first;
            // time: O(1); memory: O(1)
            it_map->second.push_back(order);
        }
        case Type::Market: {
            if (limit_book[order.side].empty()) {
                std::cerr << "Error: no market price - insert a Limit order first.\n";
                return 0;
            }
            // time: O(1); memory: O(1)
            // Iterator for a map limit_book[side] element (list)
            auto rit_map = limit_book[order.side].rbegin();
            // time: O(1); memory: O(1)
            rit_map->second.push_back(order);
            break;
        }
    }
    time++;

    return order.id;
}

bool OrderBook::cancel_order(int order_id) {
    OrderList::iterator it = locate_order(order_id);
    
    if (it->type == Type::Peg)
        // time: O(1); memory: O(1)
        peg_book[it->side].erase(it);
    else {
        // time: O(log n); memory: O(1).
        // Iterator for an map limit_book[it->side] element (list).
        auto it_map = limit_book[it->side].find(it->price);

        // time: O(1); memory: O(1).
        it_map->second.erase(it);
        if (it_map->second.empty())
            // Remove the map node. Needed to keep easy location of bid and ask.
            limit_book[it->side].erase(it_map);
    }
    // No need to advance the time.

    return true;
}

bool OrderBook::change_order(int order_id, OrderChange& order_change) {
    auto it_id = ids.find(order_id);
    if (it_id == ids.end()) {
        std::cerr << "Error: order not found.\n";
        return false;
    }
    OrderList::iterator it = it_id->second;
    
    if (it->type == Type::Peg) {
        it->qty = order_change.qty;
        // time: O(1); memory: O(1)
        peg_book[it->side].splice(peg_book[it->side].end(), peg_book[it->side], it);
    }
    else {
        // time: O(log n); memory: O(1).
        // Iterator for an map limit_book[it->side] element (list).
        auto it_map = limit_book[it->side].find(it->price);
        it->qty = order_change.qty;

        auto new_it_map = it_map;
        if (order_change.price != it->price) {
            // time: O(log n); memory: O(1).
            // Gets the other price list.
            new_it_map = limit_book[it->side].try_emplace(order_change.price).first;
            if (it_map->second.empty())
                // If the list empties, the map node must be deleted.
                limit_book[it->side].erase(it_map);
        }
        // Moves the order to the back.
        it_map->second.splice(new_it_map->second.end(), it_map->second, it);
    }
    // No need to advance the time.

    return true;
}




