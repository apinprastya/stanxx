#include "subscriber.h"
#include "client.h"
#include <memory>
#include <seastar/core/future.hh>
#include <seastar/core/shard_id.hh>
#include <seastar/core/shared_mutex.hh>
#include <vector>

namespace stanxx {

Subscriber::Subscriber (const std::string& subject,
const std::string& subId,
const std::string& clientId,
int coreId)
: _subject (subject), _id (subId), _clientId (clientId), _coreId (coreId) {
}

std::vector<std::string_view> SubscriberManager::splitTopic (const std::string& topic) const {
    std::vector<std::string_view> parts;
    parts.reserve (8);

    size_t start = 0;
    size_t pos   = 0;
    while ((pos = topic.find ('.', start)) != std::string::npos) {
        parts.push_back (std::string_view (topic.data () + start, pos - start));
        start = pos + 1;
    }
    if (start < topic.length ()) {
        parts.push_back (std::string_view (topic.data () + start, topic.length () - start));
    }
    return parts;
}

void SubscriberManager::addSubscriber (std::shared_ptr<Subscriber> subscriber) {
    cache.invalidate (subscriber->getSubject ());
    auto parts = splitTopic (subscriber->getSubject ());
    root.subscribe (parts, 0, subscriber);
}

void SubscriberManager::addSubscriber (const std::string& subject,
const std::string& id,
const std::string& clientId,
int coreId,
Client* client) {
    if (client != nullptr) {
        clients.insert_or_assign (clientId, client);
    }
    auto subscriber = std::make_shared<Subscriber> (subject, id, clientId, coreId);
    addSubscriber (subscriber);
}


void SubscriberManager::unsubscribeClientId (const std::string& id) {
    cache.clear ();
    root.removeIf ([id] (std::shared_ptr<Subscriber> item) -> bool {
        return item->getClientId () == id;
    });
    clients.erase (id);
}

void SubscriberManager::unsubscribeClientIdSubId (const std::string& id,
const std::string& subId) {
    cache.clear ();
    root.removeIf ([id, subId] (std::shared_ptr<Subscriber> item) -> bool {
        return item->getClientId () == id && item->getId () == subId;
    });
}

std::vector<std::shared_ptr<Subscriber>> SubscriberManager::getSubscriber (
const std::string& subject) {
    std::vector<std::shared_ptr<Subscriber>> result;
    if (cache.get (subject, result)) {
        return result;
    }
    auto parts = splitTopic (subject);
    root.getSubscriber (parts, 0, subject, result, false);
    cache.put (subject, result);
    return result;
}

seastar::future<> SubscriberManager::sendMessage (const std::string& clientId,
seastar::temporary_buffer<char> data) {
    auto it = clients.find (clientId);
    if (it != clients.end ()) {
        return it->second->sendMessage (std::move (data));
    }
    return seastar::make_ready_future<> ();
}

seastar::future<> ClusteredSubscriberManager::start () {
    return _subscriberManager.start ();
}

seastar::future<> ClusteredSubscriberManager::stop () {
    return _subscriberManager.stop ();
}

seastar::future<> ClusteredSubscriberManager::addSubscriber (const std::string& subject,
const std::string& id,
int coreId,
Client* client) {
    _subscriberManager.local ().addSubscriber (subject, id, client->getId (), coreId, client);
    return _subscriberManager.invoke_on_others (
    [subject, id, clientId = client->getId (), coreId] (SubscriberManager& manager) {
        return manager.addSubscriber (subject, id, clientId, coreId, nullptr);
    });
}

seastar::future<> ClusteredSubscriberManager::unsubscribeClientId (const std::string& id) {
    _subscriberManager.local ().unsubscribeClientId (id);
    return _subscriberManager.invoke_on_others (
    [id] (SubscriberManager& manager) -> seastar::future<> {
        manager.unsubscribeClientId (id);
        return seastar::make_ready_future<> ();
    });
}

seastar::future<> ClusteredSubscriberManager::unsubscribeClientIdSubId (const std::string& id,
const std::string& subId) {
    _subscriberManager.local ().unsubscribeClientIdSubId (id, subId);
    return _subscriberManager.invoke_on_others (
    [id, subId] (SubscriberManager& manager) -> seastar::future<> {
        manager.unsubscribeClientIdSubId (id, subId);
        return seastar::make_ready_future<> ();
    });
}

std::vector<std::shared_ptr<Subscriber>>
ClusteredSubscriberManager::getSubscriber (const std::string& subject) {
    return _subscriberManager.local ().getSubscriber (subject);
}

seastar::future<> ClusteredSubscriberManager::sendMessage (std::shared_ptr<Subscriber> subscriber,
seastar::temporary_buffer<char> data) {
    if (seastar::this_shard_id () == subscriber->getCoreId ()) {
        return _subscriberManager.local ().sendMessage (
        subscriber->getClientId (), std::move (data));
    }
    return _subscriberManager.invoke_on (subscriber->getCoreId (),
    [clientId = subscriber->getClientId (), shared_data = std::move (data.share ())] (
    SubscriberManager& manager) mutable -> seastar::future<> {
        return manager.sendMessage (clientId, std::move (shared_data));
    });
}

} // namespace stanxx