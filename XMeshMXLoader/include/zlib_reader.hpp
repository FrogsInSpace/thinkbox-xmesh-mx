// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#if 0
namespace frantic { namespace files {

#include <zlib.h>

#pragma warning( push )
#pragma warning( disable : 4511 4512 4800 )
#include <zfstream.h>
#pragma warning( pop )

template<class DelegateReader>
class zlib_reader {
public:
	inline zlib_reader();
	virtual ~zlib_reader() {
		close();
	}

	inline void open( const frantic::tstring& filePath );
	inline void close();

	/**
	 * Explicitly position the read pointer. Only supported before the first call to read(). Will seek in underlying file, not the de-compressed output.
	 * @param off The location to seek to, relative to the start of the stream.
	 * @return (*this)
	 */
	inline zlib_reader& seekg( std::ios::off_type off );

	/**
	 * Will read the specified number of decompressed characters from the stream and store them in the supplied buffer. Throws an exception if it cannot
	 * extract the specified amount of characters (This is different from std::istream).
	 * @param dest The destination of decompressed data
	 * @param readSize The number of decompressed characters to read.
	 * @return (*this)
	 */
	inline zlib_reader& read( char* dest, std::size_t readSize );

protected:
	virtual int init_stream( z_stream * stream ) {
		return inflateInit( stream );
	}

	DelegateReader m_delegate;

	bool m_bOpen;
	z_stream m_zstream;
};

template<class DelegateReader>
class gzip_reader : public zlib_reader<DelegateReader> {
public:
	gzip_reader() {
	}
private:
	int init_stream( z_stream * stream ) {
		return inflateInit2( stream, 16+MAX_WBITS );
	}
};

#ifdef WIN32
/**
 * This class is a Windows specific reader that uses Asynchronous I/O and two buffers to allow processing of one buffer's data while
 * the operating system (or ideally the storage driver directly via DMA) fills the other buffer. It also disables the Windows disk cache
 * for optimal throughput and control over read size.
 */
class win32_reader{
public:
	win32_reader();
	~win32_reader();

	void open( const frantic::tstring& filePath );
	void close();

	/** 
	 * Only supported before the first call to read(). Repositions the read pointer relative to the start of the file. There is an
	 * unspecified limit to how far the read pointer can be adjusted.
	 * @param off The offset from the start of the file to reposition to.
	 */
	void seekg( std::ios::off_type off );

	/**
	 * Will return a filled buffer from a previously started asynchronous read, and start a new asynchronous read. All buffers obtained
	 * from previous calls to read() are invalidated.
	 * @param[out] outSize The size of the returned buffer in bytes.
	 * @return A buffer filled with data read from the file.
	 */
	char* read( std::size_t& outSize );

private:
	//Buffer for reading data into, allocated using VirtualAlloc in order to get it aligned to a page (ie. 4K)
	LPVOID m_buffer;

	//Size of one of the buffers we are reading into. There are two, so the total allocation size is twice this number.
	DWORD m_singleBufferSize;

	//Handle to the open file.
	HANDLE m_fileHandle;

	//Will be non-zero if the last ReadFile call returned synchronously. This is the size of that read.
	DWORD m_lastSyncReadSize;
	
	//TRUE once we've read the entire file.
	BOOL m_eof;

	OVERLAPPED m_overlapped;

	//Current offset within the file of the last read operation started.
	LARGE_INTEGER m_fileOffset;

	//Total size of the file.
	LARGE_INTEGER m_fileSize;
	
	//Path to the file being read.
	frantic::tstring m_filePath;

	//Index of the buffer currently being filled by an asynchronous read operation.
	std::size_t m_curBuffer;

	//For supporting seekg(), this is an offset to apply to the front of the read buffer when returning the ptr.
	DWORD m_readOffset;
};

typedef zlib_reader<win32_reader> zlib_read_interface;
typedef gzip_reader<win32_reader> gzip_read_interface;
#else

/**
 * This is a simple portable implementation of the read interface. Used on non-Windows platforms at the moment.
 */
class basic_reader{
public:
	basic_reader();
	~basic_reader();

	void open( const frantic::tstring& filePath );
	
	void close();

	void seekg( std::ios::off_type off );

	char* read( std::size_t& outSize );

private:
	FILE* m_file;

	std::size_t m_bufferSize;
	boost::scoped_array<char> m_buffer;
};

typedef zlib_reader<basic_reader> zlib_read_interface;
typedef gzip_reader<basic_reader> gzip_read_interface;
#endif

template <class T>
zlib_reader<T>::zlib_reader(){
	m_bOpen = false;
	memset( &m_zstream, 0, sizeof(z_stream) );
}

template <class T>
void zlib_reader<T>::open( const frantic::tstring& filePath ){
	m_delegate.open( filePath );
	
	memset( &m_zstream, 0, sizeof(m_zstream) );
	if( Z_OK != init_stream( &m_zstream ) )
		throw std::runtime_error( "zlib_reader::open() inflateInit() failed:" + std::string( m_zstream.msg ? m_zstream.msg : "" ) );
	
	m_bOpen = true;
}

template <class T>
void zlib_reader<T>::close(){
	if(m_bOpen){
		inflateEnd(&m_zstream);
		m_bOpen = false;
	}

	m_delegate.close();
}

template <class T>
zlib_reader<T>& zlib_reader<T>::seekg( std::ios::off_type off ){
	m_delegate.seekg( off );
	return *this;
}

template <class T>
zlib_reader<T>& zlib_reader<T>::read( char* dest, std::size_t readSize ){
	m_zstream.avail_out = static_cast<uInt>(readSize);
	m_zstream.next_out = reinterpret_cast<Bytef*>(dest);

	do{
		if(m_zstream.avail_in == 0){
			std::size_t count;
			char* buffer = m_delegate.read( count );
			if( !buffer )
				throw std::runtime_error( "zlib_reader::read() Unexpected EOF" );

			m_zstream.avail_in = static_cast<uInt>(count);
			m_zstream.next_in = reinterpret_cast<Bytef*>(buffer);
		}

		int ret = inflate(&m_zstream, Z_NO_FLUSH);
		if(Z_OK != ret && Z_STREAM_END != ret)
			throw std::runtime_error( "zlib_reader::read() inflate() failed: " + std::string( m_zstream.msg ? m_zstream.msg : "" ) );
	}while(m_zstream.avail_out != 0);
		
	return *this;
}

} }
#endif
