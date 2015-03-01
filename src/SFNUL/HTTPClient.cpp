/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <SFNUL/HTTPClient.hpp>
#include <SFNUL/IpAddress.hpp>
#include <SFNUL/Endpoint.hpp>
#include <SFNUL/TlsConnection.hpp>
#include <SFNUL/MakeUnique.hpp>
#include <http_parser.h>
#include <algorithm>
#include <deque>

namespace sfn {

int OnMessageBegin( http_parser* parser );
int OnUrl( http_parser* parser, const char* data, std::size_t length );
int OnStatus( http_parser* parser, const char* data, std::size_t length );
int OnHeaderField( http_parser* parser, const char* data, std::size_t length );
int OnHeaderValue( http_parser* parser, const char* data, std::size_t length );
int OnHeadersComplete( http_parser* parser );
int OnBody( http_parser* parser, const char* data, std::size_t length );
int OnMessageComplete( http_parser* parser );

class HTTPClientPipeline {
public:
	HTTPClientPipeline( Endpoint endpoint, bool secure, const std::chrono::seconds& timeout ) :
		m_secure{ secure },
		m_remote_endpoint{ endpoint },
		m_timeout_value{ timeout }
	{
		if( !secure ) {
			m_socket = TcpSocket::Create();
			m_socket->Connect( endpoint );
		}
		else {
			m_socket = TlsConnection<TcpSocket, TlsEndpointType::Client, TlsVerificationType::Required>::Create();
		}

		http_parser_init( &m_parser, HTTP_RESPONSE );

		m_parser_settings.on_message_begin = OnMessageBegin;
		m_parser_settings.on_url = OnUrl;
		m_parser_settings.on_status = OnStatus;
		m_parser_settings.on_header_field = OnHeaderField;
		m_parser_settings.on_header_value = OnHeaderValue;
		m_parser_settings.on_headers_complete = OnHeadersComplete;
		m_parser_settings.on_body = OnBody;
		m_parser_settings.on_message_complete = OnMessageComplete;
	}

#if !defined( _MSC_VER )
	HTTPClientPipeline( HTTPClientPipeline&& ) = default;
#endif

	~HTTPClientPipeline() {
		if( !m_socket ) {
			return;
		}

		auto shutdown_start = std::chrono::steady_clock::now();

		m_socket->Shutdown();

		while( !m_socket->LocalHasShutdown() && ( ( std::chrono::steady_clock::now() - shutdown_start ) < std::chrono::seconds{ 1 } ) ) {
		}

		if( ( std::chrono::steady_clock::now() - shutdown_start ) >= std::chrono::seconds{ 1 } ) {
			InformationMessage() << "HTTP Connection shutdown timed out.\n";
		}

		m_socket->ClearBuffers();
		m_socket->Reset();
		m_socket->Close();
	}

	void LoadCertificate( TlsCertificate::Ptr certificate, const std::string& common_name ) {
		if( !m_secure ) {
			return;
		}

		m_certificate = certificate;

		if( m_socket ) {
			auto socket = std::static_pointer_cast<TlsConnection<TcpSocket, TlsEndpointType::Client, TlsVerificationType::Required>>( m_socket );

			if( !common_name.empty() ) {
				socket->SetPeerCommonName( common_name );
			}

			socket->AddTrustedCertificate( std::move( certificate ) );
			socket->Connect( m_remote_endpoint );
		}
	}

	void SendRequest( HTTPRequest request ) {
		if( m_pipeline.empty() ) {
			m_current_request = request;
		}

		auto request_string = request.ToString();

		m_socket->Send( request_string.c_str(), request_string.length() );
		m_last_activity = std::chrono::steady_clock::now();

		m_pipeline.emplace_back( std::move( request ), HTTPResponse{} );
	}

	HTTPResponse GetResponse( const HTTPRequest& request ) {
		auto iter( std::begin( m_pipeline ) );

		for( ; iter != std::end( m_pipeline ); ++iter ) {
			if( iter->first == request ) {
				break;
			}
		}

		if( iter == std::end( m_pipeline ) ) {
			return HTTPResponse{};
		}

		if( !iter->second.IsComplete() ) {
			return iter->second;
		}

		auto result = std::move( iter->second );

		m_pipeline.erase( iter );

		return result;
	}

	void Update() {
		if( TimedOut() ) {
			if( HasRequests() ) {
				m_last_activity = std::chrono::steady_clock::now();
				Reconnect();
			}

			return;
		}

		std::vector<char> data( m_socket->BytesToReceive() );

		std::size_t received = 0;

		while( ( received = m_socket->Receive( data.data(), data.size() ) ) ) {
			if( received ) {
				m_last_activity = std::chrono::steady_clock::now();
			}
			else {
				return;
			}

			m_parser.data = this;

			auto parsed = http_parser_execute( &m_parser, &m_parser_settings, data.data(), received );

			if( parsed != received ) {
				ErrorMessage() << "HTTP Parser parsed " << parsed << " of " << received << " bytes.\n";
				ErrorMessage() << "HTTP Parser error: " << http_errno_description( HTTP_PARSER_ERRNO( &m_parser ) ) << ".\n";
				return;
			}

			auto new_buffer_size = m_socket->BytesToReceive();

			if( new_buffer_size > data.size() ) {
				data.resize( new_buffer_size );
			}
		}

		if( m_socket->RemoteHasShutdown() && !m_socket->LocalHasShutdown() ) {
			http_parser_execute( &m_parser, &m_parser_settings, nullptr, 0 );
			Reconnect();
		}
	}

	bool TimedOut() const {
		return ( m_timeout_value != std::chrono::seconds{ 0 } ) && ( ( std::chrono::steady_clock::now() - m_last_activity ) > m_timeout_value );
	}

	bool HasRequests() const {
		return !m_pipeline.empty();
	}
private:
	friend int OnMessageBegin( http_parser* parser );
	friend int OnUrl( http_parser* parser, const char* data, std::size_t length );
	friend int OnStatus( http_parser* parser, const char* data, std::size_t length );
	friend int OnHeaderField( http_parser* parser, const char* data, std::size_t length );
	friend int OnHeaderValue( http_parser* parser, const char* data, std::size_t length );
	friend int OnHeadersComplete( http_parser* parser );
	friend int OnBody( http_parser* parser, const char* data, std::size_t length );
	friend int OnMessageComplete( http_parser* parser );

	typedef std::pair<HTTPRequest, HTTPResponse> PipelineElement;
	typedef std::deque<PipelineElement> Pipeline;

	void Reconnect() {
		m_socket->Shutdown();

		auto send_start = std::chrono::steady_clock::now();

		while( !m_socket->LocalHasShutdown() && ( ( std::chrono::steady_clock::now() - send_start ) < std::chrono::seconds{ 1 } ) ) {
		}

		m_socket->ClearBuffers();
		m_socket->Reset();
		m_socket->Close();

		if( !m_secure ) {
			m_socket = TcpSocket::Create();
		}
		else {
			m_socket = TlsConnection<TcpSocket, TlsEndpointType::Client, TlsVerificationType::Required>::Create();

			auto socket = std::static_pointer_cast<TlsConnection<TcpSocket, TlsEndpointType::Client, TlsVerificationType::Required>>( m_socket );

			socket->AddTrustedCertificate( m_certificate );
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

	TcpSocket::Ptr m_socket;

	bool m_secure;
	Endpoint m_remote_endpoint;
	TlsCertificate::Ptr m_certificate;

#if defined( __GNUG__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

	http_parser_settings m_parser_settings;
	http_parser m_parser;

#if defined( __GNUG__ )
#pragma GCC diagnostic pop
#endif

	Pipeline m_pipeline;

	HTTPRequest m_current_request;

	std::string m_last_header_field;
	bool m_header_field_complete{ false };

	std::chrono::steady_clock::time_point m_last_activity{ std::chrono::steady_clock::now() };

	std::chrono::seconds m_timeout_value;
};

int OnMessageBegin( http_parser* /*parser*/ ) {
	//HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	return 0;
}

int OnUrl( http_parser* /*parser*/, const char* /*data*/, std::size_t /*length*/ ) {
	//HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	return 0;
}

int OnStatus( http_parser* parser, const char* data, std::size_t length ) {
	HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	auto iter( std::begin( pipeline.m_pipeline ) );

	for( ; iter != std::end( pipeline.m_pipeline ); ++iter ) {
		if( iter->first == pipeline.m_current_request ) {
			break;
		}
	}

	if( iter == std::end( pipeline.m_pipeline ) ) {
		ErrorMessage() << "HTTP Parser could not find pipeline element to update.\n";
		return 1;
	}

	HTTPResponse& response( iter->second );

	response.SetStatus( response.GetStatus() + std::string( data, length ) );

	return 0;
}

int OnHeaderField( http_parser* parser, const char* data, std::size_t length ) {
	sfn::HTTPClientPipeline& pipeline( *static_cast<sfn::HTTPClientPipeline*>( parser->data ) );

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
		ErrorMessage() << "HTTP Parser could not find pipeline element to update.\n";
		return 1;
	}

	HTTPResponse& response( iter->second );

	response.SetHeaderValue( pipeline.m_last_header_field, response.GetHeaderValue( pipeline.m_last_header_field ) + std::string( data, length ) );

	return 0;
}

int OnHeadersComplete( http_parser* parser ) {
	HTTPClientPipeline& pipeline( *static_cast<HTTPClientPipeline*>( parser->data ) );

	auto iter( std::begin( pipeline.m_pipeline ) );

	for( ; iter != std::end( pipeline.m_pipeline ); ++iter ) {
		if( iter->first == pipeline.m_current_request ) {
			break;
		}
	}

	if( iter == std::end( pipeline.m_pipeline ) ) {
		ErrorMessage() << "HTTP Parser could not find pipeline element to update.\n";
		return 1;
	}

	HTTPResponse& response( iter->second );

	response.SetHeaderComplete();

	auto content_length = response.GetHeaderValue( "Content-Length" );
	if( !content_length.empty() ) {
		response.ReserveBody( static_cast<std::size_t>( std::stoi( content_length ) ) );
	}

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
		ErrorMessage() << "HTTP Parser could not find pipeline element to update.\n";
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
		ErrorMessage() << "HTTP Parser could not find pipeline element to update.\n";
		return 1;
	}

	iter->second.SetBodyComplete();

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

HTTPClient::HTTPClient() = default;

HTTPClient::~HTTPClient() = default;

void HTTPClient::SendRequest( HTTPRequest request, const std::string& address, unsigned short port, bool secure ) {
	auto addresses = sfn::IpAddress::Resolve( address );

	if( addresses.empty() ) {
		WarningMessage() << "HTTP name resolution failed for " << address << "\n";
		return;
	}

	Endpoint endpoint{ addresses.front(), port };

	auto iter = std::find_if( std::begin( m_pipelines ), std::end( m_pipelines ),
		[address, port]( const Pipeline& pipeline ) {
			return ( ( std::get<1>( pipeline ) == address ) && ( std::get<2>( pipeline ) == port ) );
		}
	);

	if( iter == std::end( m_pipelines ) ) {
		m_pipelines.emplace_back( make_unique<HTTPClientPipeline>( std::move( endpoint ), secure, m_timeout_value ), address, port );

		iter = std::end( m_pipelines );
		--iter;

		auto certificate_iter = m_certificates.find( address );

		if( certificate_iter != std::end( m_certificates ) ) {
			std::get<0>( *iter )->LoadCertificate( certificate_iter->second.first, certificate_iter->second.second );
		}
	}

	auto str = request.ToString();
	std::get<0>( *iter )->SendRequest( request );
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

	return std::get<0>( *pipeline_iter )->GetResponse( request );
}

void HTTPClient::LoadCertificate( const std::string& address, TlsCertificate::Ptr certificate, std::string common_name ) {
	m_certificates[address] = std::make_pair( certificate, common_name );

	auto pipeline_iter = std::find_if( std::begin( m_pipelines ), std::end( m_pipelines ),
		[&]( Pipeline& p ) {
			return ( address == std::get<1>( p ) );
		}
	);

	if( pipeline_iter == std::end( m_pipelines ) ) {
		return;
	}

	std::get<0>( *pipeline_iter )->LoadCertificate( std::move( certificate ), common_name );
}

void HTTPClient::SetTimeoutValue( const std::chrono::seconds& timeout ) {
	m_timeout_value = timeout;
}

void HTTPClient::Update() {
	for( auto iter = std::begin( m_pipelines ); iter != std::end( m_pipelines ); ) {
		std::get<0>( *iter )->Update();

		if( std::get<0>( *iter )->TimedOut() ) {
			if( !std::get<0>( *iter )->HasRequests() ) {
				iter = m_pipelines.erase( iter );
			}
		}
		else {
			++iter;
		}
	}
}

}
