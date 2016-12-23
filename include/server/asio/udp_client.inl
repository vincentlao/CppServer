/*!
    \file udp_client.inl
    \brief UDP client inline implementation
    \author Ivan Shynkarenka
    \date 23.12.2016
    \copyright MIT License
*/

namespace CppServer {
namespace Asio {

inline UDPClient::UDPClient(std::shared_ptr<Service> service, const std::string& address, int port)
    : _id(CppCommon::UUID::Generate()),
      _service(service),
      _endpoint(asio::ip::udp::endpoint(asio::ip::address::from_string(address), port)),
      _socket(_service->service(), asio::ip::udp::endpoint(_endpoint.protocol(), 0)),
      _connected(false),
      _reciving(false),
      _sending(false)
{
}

inline UDPClient::UDPClient(std::shared_ptr<Service> service, const asio::ip::udp::endpoint& endpoint)
    : _id(CppCommon::UUID::Generate()),
      _service(service),
      _endpoint(endpoint),
      _socket(_service->service(), asio::ip::udp::endpoint(_endpoint.protocol(), 0)),
      _connected(false),
      _reciving(false),
      _sending(false)
{
}

inline bool UDPClient::Connect()
{
    if (!_service->IsStarted())
        return false;

    if (IsConnected())
        return false;

    // Post the connect routine
    auto self(this->shared_from_this());
    _service->service().post([this, self]()
    {
        // Update the connected flag
        _connected = true;

        // Call the client connected handler
        onConnected();

        // Try to receive something from the server
        TryReceive();
    });

    return true;
}

inline bool UDPClient::Disconnect()
{
    if (!IsConnected())
        return false;

    // Post the disconnect routine
    auto self(this->shared_from_this());
    _service->service().post([this, self]()
    {
        // Update the connected flag
        _connected = false;

        // Call the client disconnected handler
        onDisconnected();

        // Clear receive/send buffers
        _recive_buffer.clear();
        {
            std::lock_guard<std::mutex> locker(_send_lock);
            _send_buffer.clear();
        }
    });

    return true;
}

inline size_t UDPClient::Send(const void* buffer, size_t size)
{
    // Send the datagram to the server endpoint
    return Send(_endpoint, buffer, size);
}

inline size_t UDPClient::Send(const asio::ip::udp::endpoint& endpoint, const void* buffer, size_t size)
{
    std::lock_guard<std::mutex> locker(_send_lock);

    const uint8_t* bytes = (const uint8_t*)buffer;
    _send_buffer.insert(_send_buffer.end(), bytes, bytes + size);

    // Dispatch the send routine
    auto self(this->shared_from_this());
    service()->service().dispatch([this, self, endpoint, size]()
    {
        // Try to send the datagram
        TrySend(endpoint, size);
    });

    return _send_buffer.size();
}

inline void UDPClient::TryReceive()
{
    if (_reciving)
        return;

    // Prepare receive buffer
    size_t old_size = _recive_buffer.size();
    _recive_buffer.resize(_recive_buffer.size() + CHUNK);

    _reciving = true;
    auto self(this->shared_from_this());
    _socket.async_receive_from(asio::buffer(_recive_buffer.data(), _recive_buffer.size()), _recive_endpoint, [this, self](std::error_code ec, size_t received)
    {
        _reciving = false;

        // Received datagram from the server
        if (received > 0)
        {
            // Prepare receive buffer
            _recive_buffer.resize(_recive_buffer.size() - (CHUNK - received));

            // Call the datagram received handler
            onReceived(_recive_endpoint, _recive_buffer.data(), _recive_buffer.size());

            // Clear the handled buffer
            _recive_buffer.clear();
        }

        // Try to receive again if the session is valid
        if (!ec || (ec == asio::error::would_block))
            TryReceive();
        else
        {
            onError(ec.value(), ec.category().name(), ec.message());
            Disconnect();
        }
    });
}

inline void UDPClient::TrySend(const asio::ip::udp::endpoint& endpoint, size_t size)
{
    if (_sending)
        return;

    _sending = true;
    auto self(this->shared_from_this());
    _socket.async_send_to(asio::const_buffer(_send_buffer.data(), _send_buffer.size()), endpoint, [this, self, endpoint](std::error_code ec, size_t sent)
    {
        _sending = false;

        // Sent datagram to the server
        if (sent > 0)
        {
            // Erase the sent buffer
            _send_buffer.erase(_send_buffer.begin(), _send_buffer.begin() + sent);

            // Call the datagram sent handler
            onSent(endpoint, sent, 0);

            // Stop sending
            return;
        }

        // Try to send again if the session is valid
        if (!ec || (ec == asio::error::would_block))
            return;
        else
        {
            onError(ec.value(), ec.category().name(), ec.message());
            Disconnect();
        }
    });
}

} // namespace Asio
} // namespace CppServer