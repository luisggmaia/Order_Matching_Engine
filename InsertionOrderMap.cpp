#include <list>
#include <unordered_map>
#include <stdexcept>
#include "InsertionOrderMap.hpp"

// class InsertionOrderMap

template <typename Key, typename Value>
class InsertionOrderMap {
    public:
        struct Element {
            Element(Key k, Value v) : key(std::move(k)), value(std::move(v)) {}

            const Key& get_key() const { return key; }
            Value value;
            private:
                Key key;
                friend class InsertionOrderMap<Key, Value>;
        };

        using iterator = typename std::list<Element>::iterator;
        using const_iterator = typename std::list<Element>::const_iterator;

    private:
        std::list<Element> list;
        std::unordered_map<Key, iterator> index;

    public:

        bool change_key(iterator it, const Key& new_key) {
            // time: O(1); memory: O(1)
            if (index.contains(new_key))
                return false;

            index.erase(it->key);
            index[new_key] = it;
            it->key = new_key;

            return true;
        }

        void push_back(const Key& key, const Value& value) {
            // time: O(1); memory: O(1)
            if (index.contains(key))
                throw std::runtime_error("Error: key already exists.\n");
            list.push_back(Element{key, value});
            index[key] = std::prev(list.end());
        }

        void push_front(const Key& key, const Value& value) {
            // time: O(1); memory: O(1)
            if (index.contains(key))
                throw std::runtime_error("Error: key already exists.\n");
            list.push_front(Element{key, value});
            index[key] = list.begin();
        }

        bool splice(const_iterator pos, InsertionOrderMap& other, iterator it) {
            // time: O(1); memory: O(1)
            bool same_map = (&other == this);
            if (!same_map && index.contains(it->key))
                throw std::runtime_error("Error: key already exists.\n");
            list.splice(pos, other.list, it);
            if (!same_map) {
                other.index.erase(it->key);
                index[it->key] = it;
            }
            return true;
        }

        void pop_back() {
            // time: O(1); memory: O(1)
            if (list.empty())
                return;
            list.pop_back();
            index.erase(list.back().key);
        }

        void pop_front() {
            // time: O(1); memory: O(1)
            if (list.empty())
                return;
            list.pop_front();
            index.erase(list.front().key);
        }

        bool erase(const Key& key) {
            // time: O(1); memory: O(1)
            auto it = index.find(key);
            if (it == index.end())
                throw std::runtime_error("Error: key does not exist.\n");
            list.erase(it->second);
            index.erase(it);
            return true;
        }

        void clear() {
            // time: O(n); memory: O(1)
            list.clear();
            index.clear();
        }

        iterator find(const Key& key) {
            // time: O(1); memory: O(1)
            auto it = index.find(key);
            return (it != index.end()) ? it->second : list.end();
        }

        bool contains(const Key& key) {
            // time: O(1); memory: O(1)
            return index.contains(key);
        }

        size_t size() const noexcept { return list.size(); }
        bool empty() const noexcept { return list.empty(); }

        iterator begin() { return list.begin(); }
        iterator end() { return list.end(); }
        Element& front() { return list.front(); }
        Element& back() { return list.back(); }

};
