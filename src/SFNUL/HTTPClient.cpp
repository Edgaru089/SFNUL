/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <iostream>
#include <algorithm>
#include <SFNUL/HTTPClient.hpp>
#include <SFNUL/IpAddress.hpp>
#include <SFNUL/Endpoint.hpp>
#include <SFNUL/TlsConnection.hpp>

namespace sfn {

/// @cond

int OnMessageBegin( http_parser* /*parser*/ ) {
	//HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	return 0;
}

int OnUrl( http_parser* /*parser*/, const char* /*data*/, std::size_t /*length*/ ) {
	//HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	return 0;
}

int OnStatusComplete( http_parser* /*parser*/ ) {
	//HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	return 0;
}

int OnHeaderField( http_parser* parser, const char* data, std::size_t length ) {
	HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	// If this is a new header, clear the saved field name and set complete to false.
	if( pipeline.m_header_field_complete ) {
		pipeline.m_last_header_field.clear();
		pipeline.m_header_field_complete = false;
	}

	pipeline.m_last_header_field.append( data, length );

	return 0;
}

int OnHeaderValue( http_parser* parser, const char* data, std::size_t length ) {
	HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	pipeline.m_header_field_complete = true;

	auto iter( std::begin( pipeline.m_pipeline ) );

	for( ; iter != std::end( pipeline.m_pipeline ); ++iter ) {
		if( iter->first == pipeline.m_current_request ) {
			break;
		}
	}

	if( iter == std::end( pipeline.m_pipeline ) ) {
		std::cerr << "Error: HTTP Parser could not find pipeline element to update.\n";
		return 1;
	}

	HTTPResponse& response( iter->second );

	response.SetHeaderValue( pipeline.m_last_header_field, response.GetHeaderValue( pipeline.m_last_header_field ) + std::string( data, length ) );

	return 0;
}

int OnHeadersComplete( http_parser* /*parser*/ ) {
	//HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	return 0;
}

int OnBody( http_parser* parser, const char* data, std::size_t length ) {
	HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	auto iter( std::begin( pipeline.m_pipeline ) );

	for( ; iter != std::end( pipeline.m_pipeline ); ++iter ) {
		if( iter->first == pipeline.m_current_request ) {
			break;
		}
	}

	if( iter == std::end( pipeline.m_pipeline ) ) {
		std::cerr << "Error: HTTP Parser could not find pipeline element to update.\n";
		return 1;
	}

	HTTPResponse& response( iter->second );

	auto body = response.GetBody();

	body.insert( std::end( body ), data, data + length );

	response.SetBody( std::move( body ) );

	return 0;
}

int OnMessageComplete( http_parser* parser ) {
	HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	auto iter( std::begin( pipeline.m_pipeline ) );

	for( ; iter != std::end( pipeline.m_pipeline ); ++iter ) {
		if( iter->first == pipeline.m_current_request ) {
			break;
		}
	}

	if( iter == std::end( pipeline.m_pipeline ) ) {
		std::cerr << "Error: HTTP Parser could not find pipeline element to update.\n";
		return 1;
	}

	iter->second.SetComplete();

	if( iter->second.GetHeaderValue( "Connection" ) == "close" ) {
		pipeline.Reconnect();
	}

	++iter;

	if( iter == std::end( pipeline.m_pipeline ) ) {
		pipeline.m_current_request = HTTPRequest{};
		return 0;
	}

	pipeline.m_current_request = iter->first;

	return 0;
}

HTTPClientPipeline::HTTPClientPipeline( Endpoint endpoint, bool secure ) {
	if( !secure ) {
		m_socket = TcpSocket::Create();
	}
	else {
		m_socket = TlsConnection<TcpSocket, TlsEndpointType::CLIENT, TlsVerificationType::NONE>::Create();
	}

	m_secure = secure;
	m_remote_endpoint = endpoint;

	m_socket->Connect( endpoint );

	http_parser_init( &m_parser, HTTP_RESPONSE );

	m_parser_settings.on_message_begin = OnMessageBegin;
	m_parser_settings.on_url = OnUrl;
	m_parser_settings.on_status_complete = OnStatusComplete;
	m_parser_settings.on_header_field = OnHeaderField;
	m_parser_settings.on_header_value = OnHeaderValue;
	m_parser_settings.on_headers_complete = OnHeadersComplete;
	m_parser_settings.on_body = OnBody;
	m_parser_settings.on_message_complete = OnMessageComplete;
}

HTTPClientPipeline::~HTTPClientPipeline() {
	if( !m_socket ) {
		return;
	}

	sf::Clock shutdown_clock;

	m_socket->Shutdown();

	while( !m_socket->RemoteHasShutdown() && shutdown_clock.getElapsedTime() < sf::seconds( 2 ) ) {
	}

	if( shutdown_clock.getElapsedTime() >= sf::seconds( 2 ) ) {
		std::cerr << "HTTP Connection shutdown timed out.\n";
	}
}

void HTTPClientPipeline::SendRequest( HTTPRequest request ) {
	if( m_pipeline.empty() ) {
		m_current_request = request;
	}

	auto request_string = request.ToString();

	m_socket->Send( request_string.c_str(), request_string.length() );
	m_timeout_timer.restart();

	m_pipeline.emplace_back( std::move( request ), HTTPResponse{} );
}

HTTPResponse HTTPClientPipeline::GetResponse( const HTTPRequest& request ) {
	auto iter( std::begin( m_pipeline ) );

	for( ; iter != std::end( m_pipeline ); ++iter ) {
		if( iter->first == request ) {
			break;
		}
	}

	if( ( iter == std::end( m_pipeline ) ) || !iter->second.IsComplete() ) {
		return HTTPResponse{};
	}

	auto result = std::move( iter->second );

	m_pipeline.erase( iter );

	return result;
}

void HTTPClientPipeline::Update() {
	if( TimedOut() ) {
		if( HasRequests() ) {
			m_timeout_timer.restart();
			Reconnect();
		}

		return;
	}

	std::array<char, 4096> data;

	std::size_t received = 0;

	while( ( received = m_socket->Receive( data.data(), data.size() ) ) ) {
		if( received ) {
			m_timeout_timer.restart();
		}
		else {
			return;
		}

		m_parser.data = this;

		auto parsed = http_parser_execute( &m_parser, &m_parser_settings, data.data(), received );

		if( parsed != received ) {
			std::cerr << "Error: HTTP Parser parsed " << parsed << " of " << received << " bytes.\n";
			std::cerr << "Error: HTTP Parser error: " << http_errno_description( HTTP_PARSER_ERRNO( &m_parser ) ) << ".\n";
			return;
		}
	}

	if( m_socket->RemoteHasShutdown() && !m_socket->LocalHasShutdown() ) {
		http_parser_execute( &m_parser, &m_parser_settings, nullptr, 0 );
		Reconnect();
	}
}

void HTTPClientPipeline::Reconnect() {
	m_socket->Shutdown();

	sf::Clock send_timer;

	while( m_socket->BytesToSend() && !m_socket->RemoteHasShutdown() && ( send_timer.getElapsedTime() < sf::seconds( 1 ) ) ) {
	}

	m_socket->ClearBuffers();
	m_socket->Close();

	if( !m_secure ) {
		m_socket = TcpSocket::Create();
	}
	else {
		m_socket = TlsConnection<TcpSocket, TlsEndpointType::CLIENT, TlsVerificationType::NONE>::Create();
	}

	m_socket->Connect( m_remote_endpoint );

	std::memset( &m_parser, 0, sizeof( http_parser ) );
	http_parser_init( &m_parser, HTTP_RESPONSE );

	bool current_set = false;

	for( auto& e : m_pipeline ) {
		if( !e.second.IsComplete() ) {
			if( !current_set ) {
				current_set = true;
				m_current_request = e.first;
			}

			auto request_string( e.first.ToString() );

			m_socket->Send( request_string.c_str(), request_string.length() );
		}
	}
}

bool HTTPClientPipeline::TimedOut() const {
	return m_timeout_timer.getElapsedTime() > m_timeout_value;
}

bool HTTPClientPipeline::HasRequests() const {
	return !m_pipeline.empty();
}

/// @endcond

void HTTPClient::SendRequest( HTTPRequest request, const std::string& address, unsigned short port, bool secure ) {
	auto addresses = sfn::IpAddress::Resolve( address );

	if( addresses.empty() ) {
		std::cerr << "Warning: HTTP name resolution failed for " << address << "\n";
		return;
	}

	Endpoint endpoint{ addresses.front(), port };

	auto iter = std::find_if( std::begin( m_pipelines ), std::end( m_pipelines ),
		[address, port]( const Pipeline& pipeline ) {
			return ( ( std::get<1>( pipeline ) == address ) && ( std::get<2>( pipeline ) == port ) );
		}
	);

	if( iter == std::end( m_pipelines ) ) {
		m_pipelines.emplace_back( HTTPClientPipeline{ std::move( endpoint ), secure }, std::move( address ), port );

		iter = std::end( m_pipelines );
		--iter;
	}

	auto str = request.ToString();
	std::get<0>( *iter ).SendRequest( request );
}


HTTPResponse HTTPClient::GetResponse( const HTTPRequest& request, const std::string& address, unsigned short port ) {
	auto pipeline_iter = std::find_if( std::begin( m_pipelines ), std::end( m_pipelines ),
		[&]( Pipeline& p ) {
			return ( ( address == std::get<1>( p ) ) && ( port == std::get<2>( p ) ) );
		}
	);

	if( pipeline_iter == std::end( m_pipelines ) ) {
		return HTTPResponse{};
	}

	return std::get<0>( *pipeline_iter ).GetResponse( request );
}

void HTTPClient::Update() {
	for( auto iter = std::begin( m_pipelines ); iter != std::end( m_pipelines ); ) {
		std::get<0>( *iter ).Update();

		if( std::get<0>( *iter ).TimedOut() ) {
			if( !std::get<0>( *iter ).HasRequests() ) {
				iter = m_pipelines.erase( iter );
			}
		}
		else {
			++iter;
		}
	}
}

}
