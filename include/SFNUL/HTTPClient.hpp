/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <list>
#include <utility>
#include <deque>
#include <tuple>
#include <SFML/System.hpp>
#include <http_parser.h>
#include <SFNUL/Config.hpp>
#include <SFNUL/HTTP.hpp>
#include <SFNUL/TcpSocket.hpp>
#include <SFNUL/Endpoint.hpp>

namespace sfn {

/// @cond

class SFNUL_API HTTPClientPipeline {
public:
	HTTPClientPipeline( Endpoint endpoint, bool secure = false );

	HTTPClientPipeline( HTTPClientPipeline&& ) = default;

	~HTTPClientPipeline();

	void SendRequest( HTTPRequest request );

	HTTPResponse GetResponse( const HTTPRequest& request );

	void Update();

	bool TimedOut() const;

	bool HasRequests() const;
private:
	friend int OnMessageBegin( http_parser* parser );
	friend int OnUrl( http_parser* parser, const char* data, std::size_t length );
	friend int OnStatusComplete( http_parser* parser );
	friend int OnHeaderField( http_parser* parser, const char* data, std::size_t length );
	friend int OnHeaderValue( http_parser* parser, const char* data, std::size_t length );
	friend int OnHeadersComplete( http_parser* parser );
	friend int OnBody( http_parser* parser, const char* data, std::size_t length );
	friend int OnMessageComplete( http_parser* parser );

	typedef std::pair<HTTPRequest, HTTPResponse> PipelineElement;
	typedef std::deque<PipelineElement> Pipeline;

	void Reconnect();

	Endpoint m_endpoint{};
	TcpSocket::Ptr m_socket{};

	bool m_secure{ false };
	Endpoint m_remote_endpoint{};

#if defined( __GNUG__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

	http_parser_settings m_parser_settings{};
	http_parser m_parser{};

#if defined( __GNUG__ )
#pragma GCC diagnostic pop
#endif

	Pipeline m_pipeline{};

	HTTPRequest m_current_request{};

	std::string m_last_header_field{};
	bool m_header_field_complete{ false };

	sf::Clock m_timeout_timer{};

	sf::Time m_timeout_value{ sf::seconds( 15 ) };
};

/// @endcond

/** A HTTP client that supports persistent connections and request pipelining.
 */
class SFNUL_API HTTPClient {
public:
	/** Send an HTTP request to a host identified by address, port and whether it is secured by TLS or not (HTTPS).
	 * @param request Request to send to the host.
	 * @param address Address of the host.
	 * @param port Port of the host. Default: 80
	 * @param secure true to establish a TLS secured connection. Default: false
	 */
	void SendRequest( HTTPRequest request, const std::string& address, unsigned short port = 80, bool secure = false );

	/** Get a response to a previously send request from a connected host identified by address and port.
	 * @param request Associated request.
	 * @param address Address of the connected host.
	 * @param port Port of the connected host. Default: 80
	 * @return HTTP Response associated with the specified request. It might be incomplete, so check before use.
	 */
	HTTPResponse GetResponse( const HTTPRequest& request, const std::string& address, unsigned short port = 80 );

	/** Update the client and handle any pending data/operations.
	 */
	void Update();
private:
	typedef std::tuple<HTTPClientPipeline, std::string, unsigned short> Pipeline;

	std::list<Pipeline> m_pipelines{};
};

}
