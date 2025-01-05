#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace stanxx {

template <typename T> class SubjectNode {
    private:
    std::unordered_map<std::string, std::unique_ptr<SubjectNode>> children;
    std::vector<T> subscribers;

    public:
    int subscribe (const std::vector<std::string>& parts, size_t index, const T& item) {
        if (index == parts.size ()) {
            auto it = std::find_if (subscribers.begin (), subscribers.end (),
            [item] (T i) -> bool { return item == i; });
            if (it != subscribers.end ()) {
                return -1;
            }
            subscribers.push_back (item);
            return 0;
        }
        const std::string& part = parts[index];
        if (!children[part]) {
            children[part] = std::make_unique<SubjectNode> ();
        }
        return children[part]->subscribe (parts, index + 1, item);
    }

    void getSubscriber (const std::vector<std::string>& parts,
    size_t index,
    const std::string& fullTopic,
    std::vector<T>& result,
    bool skip) {

        if (index == parts.size () || skip) {
            result.insert (result.end (), subscribers.begin (), subscribers.end ());
        }

        if (index < parts.size () && children.count (parts[index])) {
            children.at (parts[index])->getSubscriber (parts, index + 1, fullTopic, result, false);
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
    Subscriber (const std::string& subject, const std::string& subId);
    inline const std::string& getSubject () const {
        return mSubject;
    }
    inline const std::string& getId () const {
        return mId;
    }

    private:
    std::string mSubject{};
    std::string mId{};
};

class SubscriberManager {
    public:
    int addSubscriber (std::shared_ptr<Subscriber> subscriber);
    void unsubscribeClientId (int id);
    std::vector<std::shared_ptr<Subscriber>> getSubscriber (const std::string& subject);

    private:
    SubjectNode<std::shared_ptr<Subscriber>> root;

    std::vector<std::string> splitTopic (const std::string& topic) const;
};

} // namespace stanxx