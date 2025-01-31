/****
 * Sming Framework Project - Open Source framework for high efficiency native ESP8266 development.
 * Created 2015 by Skurydin Alexey
 * http://github.com/SmingHub/Sming
 * All files of the Sming Core are provided under the LGPL v3 license.
 *
 * TcpServer.cpp
 *
 ****/

#include "TcpServer.h"

TcpConnection* TcpServer::createClient(tcp_pcb* clientTcp)
{
	debug_d("TCP Server createClient %sNULL\r\n", clientTcp ? "not" : "");

	if(!active) {
		debug_w("Refusing new connections. The server is shutting down");
		return nullptr;
	}

	TcpConnection* con = new TcpClient(clientTcp, TcpClientDataDelegate(&TcpServer::onClientReceive, this),
									   TcpClientCompleteDelegate(&TcpServer::onClientComplete, this));

	return con;
}

void TcpServer::setKeepAlive(uint16_t seconds)
{
	debug_d("Server keep-alive updating: %u -> %u", keepAlive, seconds);
	keepAlive = seconds;
}

bool TcpServer::listen(int port, bool useSsl)
{
	if(tcp == nullptr) {
		initialize(tcp_new());
	}

	err_t res = tcp_bind(tcp, IP_ADDR_ANY, port);
	if(res != ERR_OK) {
		return res;
	}

	this->useSsl = useSsl;

	tcp = tcp_listen(tcp);
	tcp_accept(tcp, staticAccept);

	return true;
}

err_t TcpServer::onAccept(tcp_pcb* clientTcp, err_t err)
{
	// Anti DDoS :-)
	if(system_get_free_heap_size() < minHeapSize) {
		debug_w("\r\n\r\nCONNECTION DROPPED\r\n\t(free heap: %u)\r\n\r\n", system_get_free_heap_size());
		return ERR_MEM;
	}

	// Obey any requested connection limit
	if(maxConnections != 0 && connections.count() >= maxConnections) {
		debug_w("\r\n\r\nCONNECTION DROPPED\r\n\t(Existing connections: %u)\r\n\r\n", connections.count());
		return ERR_ABRT;
	}

#ifdef NETWORK_DEBUG
	debug_d("onAccept, tcp: %p, state: %d K=%d, Free heap size=%u", clientTcp, err, connections.count(),
			system_get_free_heap_size());
#endif

	if(err != ERR_OK) {
		//closeTcpConnection(clientTcp, nullptr);
		return err;
	}

	TcpConnection* client = createClient(clientTcp);
	if(client == nullptr) {
		return ERR_MEM;
	}
	client->setTimeOut(keepAlive);

	if(useSsl) {
		if(!sslCreateSession()) {
			delete client;
			return ERR_ABRT;
		}
		if(!ssl->onAccept(client, clientTcp)) {
			delete client;
			return ERR_ABRT;
		}
	}

	client->setDestroyedDelegate(TcpConnectionDestroyedDelegate(&TcpServer::onClientDestroy, this));

	connections.add(client);
	debug_d("Opening connection. Total connections: %d", connections.count());

	onClient((TcpClient*)client);

	return ERR_OK;
}

void TcpServer::onClient(TcpClient* client)
{
	activeClients++;
	debug_d("TcpServer onClient: %s\r\n", client->getRemoteIp().toString().c_str());
	if(clientConnectDelegate) {
		clientConnectDelegate(client);
	}
}

void TcpServer::onClientComplete(TcpClient& client, bool successful)
{
	activeClients--;
	debug_d("TcpSever onComplete: %s\r\n", client.getRemoteIp().toString().c_str());
	if(clientCompleteDelegate) {
		clientCompleteDelegate(client, successful);
	}
}

bool TcpServer::onClientReceive(TcpClient& client, char* data, int size)
{
	debug_d("TcpSever onReceive: %s, %d bytes\r\n", client.getRemoteIp().toString().c_str(), size);
	if(clientReceiveDelegate) {
		return clientReceiveDelegate(client, data, size);
	}
	return true;
}

err_t TcpServer::staticAccept(void* arg, tcp_pcb* new_tcp, err_t err)
{
	auto con = static_cast<TcpServer*>(arg);

	if(con == nullptr) {
		debug_e("NO CONNECTION ON TCP");
		//closeTcpConnection(new_tcp);
		tcp_abort(new_tcp);
		return ERR_ABRT;
	} else {
		con->sleep = 0;
	}

	err_t res = con->onAccept(new_tcp, err);
	return res;
}

void TcpServer::shutdown()
{
	active = false;

	debug_i("Shutting down the server ...");

	if(tcp) {
		tcp_arg(tcp, nullptr);
		tcp_accept(tcp, nullptr);
		tcp_close(tcp);

		tcp = nullptr;
	}

	if(connections.count() == 0) {
		delete this;
		return;
	}

	for(unsigned i = 0; i < connections.count(); i++) {
		TcpConnection* connection = connections[i];
		if(connection == nullptr) {
			continue;
		}

		connection->setTimeOut(1);
	}
}

void TcpServer::onClientDestroy(TcpConnection& connection)
{
	connections.removeElement(&connection);
	debug_d("Destroying connection. Total connections: %d", connections.count());

	if(active) {
		return;
	}

	if(connections.count() == 0) {
		delete this;
	}
}
