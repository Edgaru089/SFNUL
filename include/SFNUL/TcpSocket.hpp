/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <SFNUL/Config.hpp>
#include <SFNUL/Socket.hpp>
#include <SFNUL/ReliableTransport.hpp>
#include <memory>

namespace sfn {

class Endpoint;

/** TCP socket class.
 */
class SFNUL_API TcpSocket : public Socket, public ReliableTransport, public std::enable_shared_from_this<TcpSocket> {

public:
	typedef std::shared_ptr<TcpSocket> Ptr; //!< Shared pointer.

	/** Create a TCP socket.
	 * @return A shared pointer that owns a new TCP socket.
	 */
	static Ptr Create();

	/** Destructor.
	 */
	~TcpSocket();

	/** Asynchronously connect this TCP socket to a remote endpoint.
	 * @param endpoint Remote endpoint.
	 */
	virtual void Connect( const Endpoint& endpoint ) override;

	/** Shutdown the socket for sending. This is required for graceful connection termination.
	 */
	virtual void Shutdown() override;

	/** Check if the local system has shut the socket down for sending.
	 * @return true if the local system has shut the socket down for sending.
	 */
	virtual bool LocalHasShutdown() const override;

	/** Check if the remote system has shut the socket down for sending.
	 * @return true if the remote system has shut the socket down for sending.
	 */
	virtual bool RemoteHasShutdown() const override;

	/** Check if the socket is part of an established TCP connection.
	 * @return true if the socket is part of an established TCP connection.
	 */
	virtual bool IsConnected() const override;

	/** Close the socket. This frees up the operating system resources assigned to the socket.
	 */
	virtual void Close() override;

	/// @cond
	virtual void Reset() override;
	/// @endcond

	/** Get the local endpoint of the established TCP connection this socket is part of.
	 * @return Local endpoint of the established TCP connection this socket is part of.
	 */
	virtual Endpoint GetLocalEndpoint() const override;

	/** Get the remote endpoint of the established TCP connection this socket is part of.
	 * @return Remote endpoint of the established TCP connection this socket is part of.
	 */
	virtual Endpoint GetRemoteEndpoint() const override;

	/** Queue data up for asynchronous sending over the established TCP connection this socket is part of.
	 * @param data Pointer to a block of memory containing the data to queue.
	 * @param size Size of the block of memory containing the data to queue.
	 * @return true if the data could be queued. If false is returned, retry again later.
	 */
	virtual bool Send( const void* data, std::size_t size ) override;

	/** Dequeue data that was asynchronously received over the established TCP connection this socket is part of.
	 * @param data Pointer to a block of memory that will contain the data to dequeue.
	 * @param size Size of the block of memory that will contain the data to dequeue.
	 * @return Number of bytes of data that was actually dequeued.
	 */
	virtual std::size_t Receive( void* data, std::size_t size ) override;

	/** Queue a Message up for asynchronous sending over the established TCP connection this socket is part of.
	 * @param message Message to queue.
	 * @return true if the message could be queued. If false is returned, retry again later.
	 */
	virtual bool Send( const Message& message ) override;

	/** Dequeue an Message that was asynchronously received over the established TCP connection this socket is part of.
	 * @param message Message to dequeue into.
	 * @return Size of the Message that was dequeued. This includes the size field of the Message. If no Message could be dequeued, this method will return 0.
	 */
	virtual std::size_t Receive( Message& message ) override;

	/** Clear the send and receive queues of this socket.
	 */
	virtual void ClearBuffers() override;

	/** Get the number of bytes queued for sending.
	 * @return Number of bytes queued for sending.
	 */
	virtual std::size_t BytesToSend() const override;

	/** Get the number of bytes queued for receiving.
	 * @return Number of bytes queued for receiving.
	 */
	virtual std::size_t BytesToReceive() const override;

	/** Get the seconds a socket should linger after it has been closed.
	 * @return Seconds a socket should linger after it has been closed. 0 means lingering is disabled.
	 */
	int GetLinger() const;

	/** Set the seconds a socket should linger after it has been closed.
	 * @param timeout Seconds a socket should linger after it has been closed. 0 disables lingering.
	 */
	void SetLinger( int timeout );

	/** Get whether TCP keep-alives are enabled.
	 * @return true if TCP keep-alives are enabled.
	 */
	bool GetKeepAlive() const;

	/** Set whether TCP keep-alives are enabled.
	 * @param keep_alive true to enable TCP keep-alives.
	 */
	void SetKeepAlive( bool keep_alive );

protected:
	/** Constructor.
	 */
	TcpSocket();

	/// @cond
	virtual void SetInternalSocket( void* internal_socket ) override;
	/// @endcond

private:
	class TcpSocketImpl;
	std::unique_ptr<TcpSocketImpl> m_impl;

	friend class TcpListener;
};

}
