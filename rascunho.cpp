#include <map>
#include <unordered_map>
#include <iostream>
#include "InsertionOrderMap.hpp"


// enum class Type

enum class Type : char { Limit, Market, Peg };

// enum class Side

enum class Side : char { Buy, Sell };

// struct Order

struct Order {
    Type type;
    Side side;
    int qty, price = 0, time = 0;
};

struct OrderChange {
    int qty, price = 0;
};

struct Trade {
    int qty, price;
    Order bid_order, offer_order;
};

// class OrderBook

class OrderBook {
    private:
        std::unordered_map<Side, std::map<int, InsertionOrderMap<int, int>>> book;
        std::unordered_map<Side, InsertionOrderMap<int, int>> peg_book;
        // Possible change from unordered_map to array
        // InsertionOrderMap com Key int (time) e Value int (qty)
        // map with Key int (price) e Value InsertionOrderMap<int, int>>
        int time; // order book time

    public:
        OrderBook();
        bool update_book();
        void print_book();
        bool insert_order(Order& order);
        // void locate_order(Order& order);
        bool cancel_order(Order& order);
        bool change_order(Order& order, OrderChange& order_change);

        const Trade& get_trade() const { return executed_trade; }

    private:
        // Needed variables for the update function
        std::map<int, InsertionOrderMap<int, int>>::iterator bid_it, offer_it;
        bool peg_bid, peg_offer;
        InsertionOrderMap<int, int> *bid_ptr, *offer_ptr;

        Trade executed_trade;
};

OrderBook::OrderBook() {
    book.try_emplace(Side::Buy); book.try_emplace(Side::Sell);
    peg_book.try_emplace(Side::Buy); peg_book.try_emplace(Side::Sell);
    time = 1;

    executed_trade.bid_order.side = Side::Buy; executed_trade.offer_order.side = Side::Sell;
}

bool OrderBook::update_book() {
    if (book[Side::Buy].empty() or book[Side::Sell].empty())
        return false;

    bid_it = std::prev(book[Side::Buy].end());
    offer_it = book[Side::Sell].begin();
    if (bid_it->first < offer_it->first)
        return false;

    // Evaluate whether the first pegged order has priority to the first limit order (according to the time)
    peg_bid = !peg_book[Side::Buy].empty() && (peg_book[Side::Buy].front().get_key() < bid_it->second.front().get_key());
    peg_offer = !peg_book[Side::Sell].empty() && (peg_book[Side::Sell].front().get_key() < offer_it->second.front().get_key());

    // Chooses the priority according to the time
    if (peg_bid) {
        bid_ptr = &(peg_book[Side::Buy]);
        executed_trade.bid_order.type = Type::Peg;
    }
    else {
        bid_ptr = &(bid_it->second);
        executed_trade.bid_order.type = Type::Limit;
        executed_trade.bid_order.price = bid_it->first;
    }
    if (peg_offer) {
        offer_ptr = &(peg_book[Side::Buy]);
        executed_trade.offer_order.type = Type::Peg;
    }
    else {
        offer_ptr = &(offer_it->second);
        executed_trade.offer_order.type = Type::Limit;
        executed_trade.offer_order.price = offer_it->first;
    }
    // bid_ptr = (peg_bid) ? &(peg_book[Side::Buy]) : &(bid_it->second);
    // offer_ptr = (peg_offer) ? &(peg_book[Side::Sell]) : &(offer_it->second);

    executed_trade.bid_order.time = bid_ptr->front().get_key();
    executed_trade.offer_order.time = offer_ptr->front().get_key();

    // Evaluate whether the buy qty is smaller, greater than or equal to the sell qty
    if (bid_ptr->front().value < offer_ptr->front().value) {
        // Bid's qty is smaller than offer's. Its trade is uncomplete.
        // Updates the qty for the offer order.
        offer_ptr->front().value -= bid_ptr->front().value;
        executed_trade.qty = bid_ptr->front().value;
        bid_ptr->pop_front();
        if (!peg_bid && bid_it->second.empty())
            // If the list empties
            book[Side::Buy].erase(bid_it);
    }
    else if (bid_ptr->front().value > offer_ptr->front().value) {
        // Offer's qty is smaller than bid's. Its trade is uncomplete.
        // Updates the qty for the bid order.
        bid_ptr->front().value -= offer_ptr->front().value;
        executed_trade.qty = offer_ptr->front().value;
        offer_ptr->pop_front();
        if (!peg_offer && offer_it->second.empty())
            // If the list empties
            book[Side::Sell].erase(offer_it);
    }
    else {
        executed_trade.qty = bid_ptr->front().value; // = offer_ptr->front().value;
        bid_ptr->pop_front();
        offer_ptr->pop_front();
        if (!peg_bid && bid_it->second.empty())
            book[Side::Buy].erase(bid_it);
        if (!peg_offer && offer_it->second.empty())
            book[Side::Sell].erase(offer_it);
    }

    // The executed price is determined by the prioritary order.
    executed_trade.price = (executed_trade.bid_order.time < executed_trade.offer_order.time) ? bid_it->first : offer_it->first;
}

void OrderBook::print_book() {

}

bool OrderBook::insert_order(Order& order) {
    switch (order.type) {
        case Type::Peg: {
            // time: O(1); memory: O(1)
            peg_book[order.side].push_back(time, order.qty);
            break;
        }
        case Type::Limit: {
            // time: O(log n); memory: O(1).
            // Inserts inline the price in the (order.side) map. Returns the iterator for the created InsertionOrderMap or for the existing.
            auto it_map = book[order.side].try_emplace(order.price).first;
            // time: O(1); memory: O(1)
            it_map->second.push_back(time, order.qty);
            
            /*
            if ((order.side == Side::Buy && order.price >= book[Side::Sell].begin()->first) || (order.side == Side::Sell && order.price <= book[Side::Buy].rbegin()->first))
                // There is only a need to update the book in a limit order case and when its price beats the bid (in sell case) or the ask (in buy case).
                update_book();
            break;
            */
        }
        case Type::Market: {
            if (book[order.side].empty()) {
                std::cerr << "Error: no market price - insert a Limit order first.\n";
                return false;
            }
            // time: O(1); memory: O(1)
            // Iterator for a map book[side] element (InsertionOrderMap)
            auto rit_map = book[order.side].rbegin();
            // time: O(1); memory: O(1)
            rit_map->second.push_back(time, order.qty);
            break;
        }
    }
    time++;

    return true;
}

/*
std::pair<InsertionOrderMap<int, int>::iterator, InsertionOrderMap<int, int>*>
OrderBook::locate_order(Order& order) {
    InsertionOrderMap<int, Order>* bookptr;
    if (order.type == Type::Peg)
        bookptr = &peg_book[order.side];
    else {
        auto it_map = book[order.side].find(order.price); // time: O(log n); memory: O(1). Iterator for an map book[order.side] element (InsertionOrderMap)
        if (it_map == book[order.side].end()) {
            // error
            // exit function
        }
        else
            bookptr = &it_map->second;
    }
    auto it_list = (*bookptr).find(order.time); // time: O(1); memory: O(1). Iterator for an Order (InsertionOrderMap List element)
    if (it_list == (*bookptr).end()) {
        // error
        // exit function
    }
    else
        return (it_list, bookptr);
}
*/

bool OrderBook::cancel_order(Order& order) {
    if (order.type == Type::Peg)
        // time: O(1); memory: O(1)
        peg_book[order.side].erase(order.time);
    else {
        // time: O(log n); memory: O(1).
        // Iterator for an map book[order.side] element (InsertionOrderMap).
        auto it_map = book[order.side].find(order.price);
        if (it_map == book[order.side].end()) {
            std::cerr << "Error: order not found.\n";
            return false;
        }
        else {
            // time: O(1); memory: O(1).
            it_map->second.erase(order.time);
            if (it_map->second.empty())
                // Remove the map node. Needed to keep easy location of bid and ask.
                book[order.side].erase(it_map);
        }
        // No need to update the book.
    }
    // No need to advance the time.

    return true;
}

bool OrderBook::change_order(Order& order, OrderChange& order_change) {
    if (order.type == Type::Peg) {
        // time: O(1); memory: O(1).
        // Iterator for an Order (InsertionOrderMap List element).
        auto it_list = peg_book[order.side].find(order.time);
        if (it_list == peg_book[order.side].end()) {
            std::cerr << "Error: order not found.\n";
            return false;
        }
        else {
            // time: O(1); memory: O(1)
            peg_book[order.side].change_key(it_list, time);
            it_list->value = order_change.qty;
            // time: O(1); memory: O(1)
            peg_book[order.side].splice(peg_book[order.side].end(), peg_book[order.side], it_list);
        }
    }
    else {
        // time: O(log n); memory: O(1).
        // Iterator for an map book[order.side] element (InsertionOrderMap).
        auto it_map = book[order.side].find(order.price);
        if (it_map == book[order.side].end()) {
            std::cerr << "Error: order not found.\n";
            return false;
        }
        else {
            // time: O(1); memory: O(1).
            // Iterator for an InsertionOrderMap element (Order).
            auto it_list = it_map->second.find(order.time);
            if (it_list == it_map->second.end()) {
                std::cerr << "Error: order not found.\n";
                return false;
            }
            else {
                // time: O(1); memory: O(1).
                it_map->second.change_key(it_list, time);
                it_list->value = order_change.qty;
                auto new_it_map = it_map;
                if (order_change.price != order.price) {
                    // time: O(log n); memory: O(1).
                    // Moves the order to other price list.
                    new_it_map = book[order.side].try_emplace(order_change.price).first;
                    if (it_map->second.empty())
                        // If the list empties, the map node must be deleted.
                        book[order.side].erase(it_map);
                }
                // Moves the order to the back.
                it_map->second.splice(new_it_map->second.end(), it_map->second, it_list);
            }
        }
        /*
        if ((order.side == Side::Buy && order_change.price >= book[Side::Sell].begin()->first) || (order.side == Side::Sell && order_change.price <= book[Side::Buy].rbegin()->first))
            // There is only a need to update the book in a limit order case and when its price beats the bid (in sell case) or the ask (in buy case).
            update_book();
        */
    }
    time++;

    return true;
}




