/*
===============================================================================

Minimal JSON parser / serializer for the built-in MCP server (Linux port).
Header-only, standard library only. Not a general-purpose JSON library — just
enough to speak JSON-RPC 2.0 with MCP clients.

===============================================================================
*/
#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>

namespace mcpjson {

enum Type { T_NULL, T_BOOL, T_NUM, T_STR, T_ARR, T_OBJ };

struct Value {
	Type					type = T_NULL;
	bool					b = false;
	double					num = 0.0;
	std::string				str;
	std::vector<Value>		arr;
	std::vector<std::pair<std::string, Value> >	obj;

	static Value null() { return Value(); }
	static Value boolean( bool v ) { Value x; x.type = T_BOOL; x.b = v; return x; }
	static Value number( double v ) { Value x; x.type = T_NUM; x.num = v; return x; }
	static Value string( const std::string &v ) { Value x; x.type = T_STR; x.str = v; return x; }
	static Value array() { Value x; x.type = T_ARR; return x; }
	static Value object() { Value x; x.type = T_OBJ; return x; }

	bool has( const std::string &key ) const {
		for ( size_t i = 0; i < obj.size(); i++ ) {
			if ( obj[i].first == key ) return true;
		}
		return false;
	}
	const Value *get( const std::string &key ) const {
		for ( size_t i = 0; i < obj.size(); i++ ) {
			if ( obj[i].first == key ) return &obj[i].second;
		}
		return NULL;
	}
	void set( const std::string &key, const Value &v ) { obj.push_back( std::make_pair( key, v ) ); }
	void push( const Value &v ) { arr.push_back( v ); }

	std::string serialize( bool pretty = false, int indent = 0 ) const;
};

inline std::string escapeString( const std::string &s ) {
	std::string out;
	out.reserve( s.size() + 8 );
	for ( size_t i = 0; i < s.size(); i++ ) {
		unsigned char c = (unsigned char) s[i];
		switch ( c ) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if ( c < 0x20 ) {
					char buf[8];
					snprintf( buf, sizeof( buf ), "\\u%04x", c );
					out += buf;
				} else {
					out += (char) c;
				}
		}
	}
	return out;
}

inline void serializeInto( const Value &v, std::string &out, bool pretty, int indent ) {
	char numbuf[64];
	switch ( v.type ) {
		case T_NULL:
			out += "null";
			break;
		case T_BOOL:
			out += v.b ? "true" : "false";
			break;
		case T_NUM:
			if ( v.num == std::floor( v.num ) && std::fabs( v.num ) < 1e15 ) {
				snprintf( numbuf, sizeof( numbuf ), "%lld", (long long) v.num );
			} else {
				snprintf( numbuf, sizeof( numbuf ), "%.17g", v.num );
			}
			out += numbuf;
			break;
		case T_STR:
			out += '"';
			out += escapeString( v.str );
			out += '"';
			break;
		case T_ARR: {
			out += '[';
			for ( size_t i = 0; i < v.arr.size(); i++ ) {
				if ( i ) out += ',';
				if ( pretty ) { out += '\n'; out.append( (size_t)( indent + 2 ), ' ' ); }
				serializeInto( v.arr[i], out, pretty, indent + 2 );
			}
			if ( pretty && !v.arr.empty() ) { out += '\n'; out.append( (size_t) indent, ' ' ); }
			out += ']';
			break;
		}
		case T_OBJ: {
			out += '{';
			for ( size_t i = 0; i < v.obj.size(); i++ ) {
				if ( i ) out += ',';
				if ( pretty ) { out += '\n'; out.append( (size_t)( indent + 2 ), ' ' ); }
				out += '"';
				out += escapeString( v.obj[i].first );
				out += pretty ? "\": " : "\":";
				serializeInto( v.obj[i].second, out, pretty, indent + 2 );
			}
			if ( pretty && !v.obj.empty() ) { out += '\n'; out.append( (size_t) indent, ' ' ); }
			out += '}';
			break;
		}
	}
}

inline std::string Value::serialize( bool pretty, int indent ) const {
	std::string out;
	serializeInto( *this, out, pretty, indent );
	return out;
}

namespace detail {

struct Parser {
	const char *p;
	const char *end;
	std::string err;

	void ws() {
		while ( p < end && ( *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ) ) p++;
	}
	bool fail( const char *msg ) {
		err = msg;
		return false;
	}
	bool parseValue( Value &v ) {
		ws();
		if ( p >= end ) return fail( "unexpected end of input" );
		char c = *p;
		if ( c == '{' ) return parseObject( v );
		if ( c == '[' ) return parseArray( v );
		if ( c == '"' ) {
			v.type = T_STR;
			return parseString( v.str );
		}
		if ( c == 't' ) {
			if ( end - p >= 4 && strncmp( p, "true", 4 ) == 0 ) { p += 4; v.type = T_BOOL; v.b = true; return true; }
			return fail( "bad literal" );
		}
		if ( c == 'f' ) {
			if ( end - p >= 5 && strncmp( p, "false", 5 ) == 0 ) { p += 5; v.type = T_BOOL; v.b = false; return true; }
			return fail( "bad literal" );
		}
		if ( c == 'n' ) {
			if ( end - p >= 4 && strncmp( p, "null", 4 ) == 0 ) { p += 4; v.type = T_NULL; return true; }
			return fail( "bad literal" );
		}
		return parseNumber( v );
	}
	bool parseNumber( Value &v ) {
		ws();
		char *endp = NULL;
		double d = strtod( p, &endp );
		if ( endp == p ) return fail( "bad number" );
		p = endp;
		v.type = T_NUM;
		v.num = d;
		return true;
	}
	bool parseString( std::string &out ) {
		if ( p >= end || *p != '"' ) return fail( "expected string" );
		p++;
		out.clear();
		while ( p < end ) {
			char c = *p++;
			if ( c == '"' ) return true;
			if ( c == '\\' ) {
				if ( p >= end ) return fail( "bad escape" );
				char e = *p++;
				switch ( e ) {
					case '"': out += '"'; break;
					case '\\': out += '\\'; break;
					case '/': out += '/'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'n': out += '\n'; break;
					case 'r': out += '\r'; break;
					case 't': out += '\t'; break;
					case 'u': {
						if ( end - p < 4 ) return fail( "bad \\u escape" );
						unsigned cp = 0;
						for ( int i = 0; i < 4; i++ ) {
							char h = *p++;
							cp <<= 4;
							if ( h >= '0' && h <= '9' ) cp |= (unsigned)( h - '0' );
							else if ( h >= 'a' && h <= 'f' ) cp |= (unsigned)( h - 'a' + 10 );
							else if ( h >= 'A' && h <= 'F' ) cp |= (unsigned)( h - 'A' + 10 );
							else return fail( "bad \\u escape" );
						}
						if ( cp < 0x80 ) {
							out += (char) cp;
						} else if ( cp < 0x800 ) {
							out += (char)( 0xC0 | ( cp >> 6 ) );
							out += (char)( 0x80 | ( cp & 0x3F ) );
						} else {
							out += (char)( 0xE0 | ( cp >> 12 ) );
							out += (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
							out += (char)( 0x80 | ( cp & 0x3F ) );
						}
						break;
					}
					default: return fail( "bad escape" );
				}
			} else {
				out += c;
			}
		}
		return fail( "unterminated string" );
	}
	bool parseArray( Value &v ) {
		p++; // '['
		v.type = T_ARR;
		ws();
		if ( p < end && *p == ']' ) { p++; return true; }
		for ( ;; ) {
			Value item;
			if ( !parseValue( item ) ) return false;
			v.arr.push_back( item );
			ws();
			if ( p >= end ) return fail( "unterminated array" );
			if ( *p == ',' ) { p++; continue; }
			if ( *p == ']' ) { p++; return true; }
			return fail( "expected ',' or ']'" );
		}
	}
	bool parseObject( Value &v ) {
		p++; // '{'
		v.type = T_OBJ;
		ws();
		if ( p < end && *p == '}' ) { p++; return true; }
		for ( ;; ) {
			ws();
			std::string key;
			if ( !parseString( key ) ) return false;
			ws();
			if ( p >= end || *p != ':' ) return fail( "expected ':'" );
			p++;
			Value val;
			if ( !parseValue( val ) ) return false;
			v.obj.push_back( std::make_pair( key, val ) );
			ws();
			if ( p >= end ) return fail( "unterminated object" );
			if ( *p == ',' ) { p++; continue; }
			if ( *p == '}' ) { p++; return true; }
			return fail( "expected ',' or '}'" );
		}
	}
};

} // namespace detail

inline bool parse( const std::string &text, Value &out, std::string *err = NULL ) {
	detail::Parser parser;
	parser.p = text.c_str();
	parser.end = text.c_str() + text.size();
	if ( !parser.parseValue( out ) ) {
		if ( err ) *err = parser.err;
		return false;
	}
	parser.ws();
	if ( parser.p != parser.end ) {
		if ( err ) *err = "trailing characters after JSON value";
		return false;
	}
	return true;
}

} // namespace mcpjson
