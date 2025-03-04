#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>


namespace stanxx {

class Client;

template <typename T> class SubjectNode {
    private:
    std::unordered_map<std::string, std::unique_ptr<SubjectNode>> children;
    std::vector<T> subscribers;

    public:
    int subscribe (const std::vector<std::string_view>& parts, size_t index, const T& item) {
        if (index == parts.size ()) {
            auto it = std::find_if (subscribers.begin (), subscribers.end (),
            [item] (T i) -> bool { return item == i; });
            if (it != subscribers.end ()) {
                return -1;
            }
            subscribers.push_back (item);
            return 0;
        }
        std::string part = std::string (parts[index]);
        if (!children[part]) {
            children[part] = std::make_unique<SubjectNode> ();
        }
        return children[part]->subscribe (parts, index + 1, item);
    }

    void getSubscriber (const std::vector<std::string_view>& parts,
    size_t index,
    const std::string& fullTopic,
    std::vector<T>& result,
    bool skip) {
        std::string part = std::string (parts[index]);
        if (index == parts.size () || skip) {
            result.insert (result.end (), subscribers.begin (), subscribers.end ());
        }

        if (index < parts.size () && children.count (part)) {
            children.at (part)->getSubscriber (parts, index + 1, fullTopic, result, false);
        }

        if (index < parts.size () && children.count ("*")) {
            children.at ("*")->getSubscriber (parts, index + 1, fullTopic, result, false);
        }

        if (children.count (">")) {
            children.at (">")->getSubscriber ({}, parts.size (), fullTopic, result, true);
        }
    }

    void removeIf (std::function<bool (T item)> check) {
        std::erase_if (
        subscribers, [check] (T item) -> bool { return check (item); });
        for (const auto& pair : children) {
            pair.second->removeIf (check);
        }
    }
};

template <typename T> class SubscriberNode {
    private:
    SubjectNode<T> root;

    std::vector<std::string> splitTopic (const std::string& topic) const {
        std::vector<std::string> parts;
        std::stringstream ss (topic);
        std::string segment;
        while (std::getline (ss, segment, '.')) {
            parts.push_back (segment);
        }
        return parts;
    }

    public:
    inline int subscribe (const std::string& topic, const T& name) {
        auto parts = splitTopic (topic);
        return root.subscribe (parts, 0, name);
    }

    inline std::vector<T> getSubscriber (const std::string& topic) {
        auto parts = splitTopic (topic);
        std::vector<T> result;
        root.getSubscriber (parts, 0, topic, result, false);
        return result;
    }
};

class Subscriber {
    public:
    Subscriber (const std::string& subject, const std::string& subId, Client* client);
    inline const std::string& getSubject () const {
        return _subject;
    }
    inline const std::string& getId () const {
        return _id;
    }
    inline Client* getClient () const {
        return _client;
    }

    private:
    std::string _subject{};
    std::string _id{};
    Client* _client{};
};

class SubscriberManager {
    private:
    // LRU Cache with capacity limit
    class SubscriberCache {
        private:
        struct CacheEntry {
            std::vector<std::shared_ptr<Subscriber>> subscribers;
            std::chrono::steady_clock::time_point lastAccess;
        };

        static constexpr size_t MAX_CACHE_SIZE = 1000;
        static constexpr auto CACHE_TTL        = std::chrono::seconds (60);

        mutable std::shared_mutex mutex;
        mutable std::unordered_map<std::string, CacheEntry> cache;

        public:
        bool get (const std::string& subject,
        std::vector<std::shared_ptr<Subscriber>>& result) const {
            std::shared_lock lock (mutex);
            auto it = cache.find (subject);
            if (it != cache.end ()) {
                auto& entry = it->second;
                auto now    = std::chrono::steady_clock::now ();
                if (now - entry.lastAccess < CACHE_TTL) {
                    result           = entry.subscribers;
                    entry.lastAccess = now;
                    return true;
                }
            }
            return false;
        }

        void put (const std::string& subject,
        const std::vector<std::shared_ptr<Subscriber>>& subscribers) {
            std::unique_lock lock (mutex);
            if (cache.size () >= MAX_CACHE_SIZE) {
                // Remove oldest entries
                auto now = std::chrono::steady_clock::now ();
                for (auto it = cache.begin (); it != cache.end ();) {
                    if (now - it->second.lastAccess > CACHE_TTL) {
                        it = cache.erase (it);
                    } else {
                        ++it;
                    }
                }
                // If still full, remove random entry
                if (cache.size () >= MAX_CACHE_SIZE) {
                    cache.erase (cache.begin ());
                }
            }
            cache[subject] = { subscribers, std::chrono::steady_clock::now () };
        }

        void invalidate (const std::string& subject) {
            std::unique_lock lock (mutex);
            cache.erase (subject);
        }

        void clear () {
            std::unique_lock lock (mutex);
            cache.clear ();
        }
    };

    public:
    int addSubscriber (std::shared_ptr<Subscriber> subscriber);
    void unsubscribeClientId (const std::string& id);
    std::vector<std::shared_ptr<Subscriber>> getSubscriber (const std::string& subject);

    private:
    SubjectNode<std::shared_ptr<Subscriber>> root;
    SubscriberCache cache;

    std::vector<std::string_view> splitTopic (const std::string& topic) const;
};

} // namespace stanxx