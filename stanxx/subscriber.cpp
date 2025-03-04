#include "subscriber.h"
#include "client.h"
#include <memory>
#include <vector>

namespace stanxx {

Subscriber::Subscriber (const std::string& subject, const std::string& subId, Client* client)
: _subject (subject), _id (subId), _client (client) {
}

std::vector<std::string_view> SubscriberManager::splitTopic (const std::string& topic) const {
    std::vector<std::string_view> parts;
    parts.reserve (8); // Most topics have < 8 parts

    size_t start = 0;
    size_t pos   = 0;
    while ((pos = topic.find ('.', start)) != std::string::npos) {
        parts.push_back (std::string_view (topic.data () + start, pos - start));
        start = pos + 1;
    }
    // Add the last part
    if (start < topic.length ()) {
        parts.push_back (std::string_view (topic.data () + start, topic.length () - start));
    }
    return parts;
}

int SubscriberManager::addSubscriber (std::shared_ptr<Subscriber> subscriber) {
    cache.invalidate (subscriber->getSubject ());
    auto parts = splitTopic (subscriber->getSubject ());
    return root.subscribe (parts, 0, subscriber);
}

void SubscriberManager::unsubscribeClientId (const std::string& id) {
    cache.clear ();
    root.removeIf ([id] (std::shared_ptr<Subscriber> item) -> bool {
        return item->getClient ()->getId () == id;
        return true;
    });
}

std::vector<std::shared_ptr<Subscriber>> SubscriberManager::getSubscriber (
const std::string& subject) {
    std::vector<std::shared_ptr<Subscriber>> result;
    // Try cache first
    if (cache.get (subject, result)) {
        return result;
    }

    // Cache miss - compute and store
    auto parts = splitTopic (subject);
    root.getSubscriber (parts, 0, subject, result, false);
    cache.put (subject, result);
    return result;
}

} // namespace stanxx