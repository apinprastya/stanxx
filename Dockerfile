FROM ubuntu:24.04

RUN apt update && \
    apt install -y libboost-all-dev libc-ares-dev libcrypto++-dev libfmt-dev libsctp-dev libprotobuf-dev liburing-dev libxml2-dev libyaml-cpp-dev libspdlog-dev

COPY build/stanxx-server/stanxx-server /srv/stanxx-server

EXPOSE 4222

CMD [ "/srv/stanxx-server" ]