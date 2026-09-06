#ifndef ORDERED_MAP_HPP
#define ORDERED_MAP_HPP

#include <unordered_map>
#include <map>
#include <stdexcept>
#include <utility>
#include <functional>


// class OrderedMap
//
// std::map (ordem por chave, segundo Compare) + std::unordered_map (indice
// Key -> Map::iterator) para permitir busca O(1) mantendo a ordenacao do
// std::map.
//
// Compare e o mesmo parametro de comparacao do std::map: std::less<Key>
// (padrao, ordem crescente) ou std::greater<Key> (ordem decrescente), ou
// qualquer comparador proprio. O unordered_map de indice nao depende de
// Compare, pois busca por chave nele independe de ordem.
//
// Definicao completa aqui no header: e uma classe template, entao o
// compilador precisa ver o corpo inteiro em cada translation unit que a
// instancia (nao ha ODR-safe split em .hpp/.cpp sem instanciacao explicita).

template <typename Key, typename Value, typename Compare = std::less<Key>>
class OrderedMap {
    public:
        using Map = std::map<Key, Value, Compare>;
        using iterator = typename Map::iterator;
        using const_iterator = typename Map::const_iterator;
        using reverse_iterator = typename Map::reverse_iterator;
        using const_reverse_iterator = typename Map::const_reverse_iterator;

    private:
        Map map;
        std::unordered_map<Key, iterator> index;

    public:
        OrderedMap() = default;
        explicit OrderedMap(const Compare& comp) : map(comp) {}

        Compare key_comp() const { return map.key_comp(); }

        iterator find(const Key& key) {
            // time: O(1); memory: O(1)
            auto it = index.find(key);
            return (it != index.end()) ? it->second : map.end();
        }

        const_iterator find(const Key& key) const {
            // time: O(1); memory: O(1)
            auto it = index.find(key);
            return (it != index.end()) ? const_iterator(it->second) : map.cend();
        }

        bool contains(const Key& key) const {
            // time: O(1); memory: O(1)
            return index.contains(key);
        }

        template <typename... Args>
        std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
            // time: O(1) if key exists; O(log n) otherwise; memory: O(1)
            // key existence is checked first (O(1), via the unordered_map index),
            // so the value is only constructed when the key is actually new.
            auto idx_it = index.find(key);
            if (idx_it != index.end())
                return {idx_it->second, false};

            auto [map_it, inserted] = map.try_emplace(key, std::forward<Args>(args)...);
            index.emplace(key, map_it);
            return {map_it, true};
        }

        template <typename... Args>
        std::pair<iterator, bool> emplace(const Key& key, Args&&... args) {
            // time: O(1) if key exists; O(log n) otherwise; memory: O(1)
            return try_emplace(key, std::forward<Args>(args)...);
        }

        std::pair<iterator, bool> insert(const Key& key, const Value& value) {
            // time: O(1) if key exists; O(log n) otherwise; memory: O(1)
            return try_emplace(key, value);
        }

        bool erase(const Key& key) {
            // time: O(1) index lookup + O(log n) map erase; memory: O(1)
            auto idx_it = index.find(key);
            if (idx_it == index.end())
                return false;
            map.erase(idx_it->second);
            index.erase(idx_it);
            return true;
        }

        Value& at(const Key& key) {
            // time: O(1); memory: O(1)
            auto idx_it = index.find(key);
            if (idx_it == index.end())
                throw std::out_of_range("OrderedMap::at: key does not exist.");
            return idx_it->second->second;
        }

        const Value& at(const Key& key) const {
            // time: O(1); memory: O(1)
            auto idx_it = index.find(key);
            if (idx_it == index.end())
                throw std::out_of_range("OrderedMap::at: key does not exist.");
            return idx_it->second->second;
        }

        Value& operator[](const Key& key) {
            // time: O(1) if key exists; O(log n) otherwise; memory: O(1)
            return try_emplace(key).first->second;
        }

        void clear() noexcept {
            // time: O(n); memory: O(1)
            map.clear();
            index.clear();
        }

        size_t size() const noexcept { return map.size(); }
        bool empty() const noexcept { return map.empty(); }

        iterator begin() noexcept { return map.begin(); }
        iterator end() noexcept { return map.end(); }
        const_iterator begin() const noexcept { return map.begin(); }
        const_iterator end() const noexcept { return map.end(); }
        const_iterator cbegin() const noexcept { return map.cbegin(); }
        const_iterator cend() const noexcept { return map.cend(); }

        reverse_iterator rbegin() noexcept { return map.rbegin(); }
        reverse_iterator rend() noexcept { return map.rend(); }
        const_reverse_iterator rbegin() const noexcept { return map.rbegin(); }
        const_reverse_iterator rend() const noexcept { return map.rend(); }

};

#endif // ORDERED_MAP_HPP
