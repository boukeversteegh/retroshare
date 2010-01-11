#include <math.h>
#include <assert.h>
#include <stdlib.h>
#include <rsiface/rspeers.h>
#include "ftchunkmap.h"

static const uint32_t SOURCE_CHUNK_MAP_UPDATE_PERIOD = 60 ; //! TTL for  chunkmap info

std::ostream& operator<<(std::ostream& o,const ftChunk& c)
{
	return o << "\tChunk [" << c.offset << "] size: " << c.size << "  ChunkId: " << c.id << "  Age: " << time(NULL) - c.ts ;
}

// Chunk: very bold implementation for now. We should compress the bits to have
// 32 of them per uint32_t value, of course!
//
Chunk::Chunk(uint64_t start,uint32_t size)
	: _start(start),_offset(start),_end( (uint64_t)size + start )
{
}

void Chunk::getSlice(uint32_t size_hint,ftChunk& chunk)
{
	// Take the current offset
	chunk.offset = _offset ;
	chunk.size = std::min(size_hint,(uint32_t)(_end-_offset)) ;
	chunk.id = _offset ;
	chunk.ts = time(NULL) ;
	
	// push the slice marker into currently handled slices.
	_offset += chunk.size ;
}

ChunkMap::ChunkMap(uint64_t s)
	:_file_size(s),_chunk_size(1024*1024) 	// 1MB chunks
{
	uint64_t n = s/(uint64_t)_chunk_size ;
	if(s% (uint64_t)_chunk_size != 0)
		++n ;

	_map.resize(n,FileChunksInfo::CHUNK_OUTSTANDING) ;
	_total_downloaded = 0 ;
	_strategy = FileChunksInfo::CHUNK_STRATEGY_STREAMING ;
#ifdef DEBUG_FTCHUNK
	std::cerr << "*** ChunkMap::ChunkMap: starting new chunkmap:" << std::endl ; 
	std::cerr << "   File size: " << s << std::endl ;
	std::cerr << "   Strategy: " << _strategy << std::endl ;
	std::cerr << "   ChunkSize: " << _chunk_size << std::endl ;
	std::cerr << "   Number of Chunks: " << n << std::endl ;
#endif
}

void ChunkMap::setAvailabilityMap(const CompressedChunkMap& map)
{
	for(uint32_t i=0;i<_map.size();++i)
		if(map[i] > 0)
		{
			_map[i] = FileChunksInfo::CHUNK_DONE ;
			_total_downloaded += _chunk_size ;
		}
		else
			_map[i] = FileChunksInfo::CHUNK_OUTSTANDING ;
}

//ChunkMap::ChunkMap(uint64_t file_size,
//							const std::vector<uint32_t>& map,
//							uint32_t chunk_size,
//							uint32_t chunk_number,
//							FileChunksInfo::ChunkStrategy strategy) 
//
//	:_file_size(file_size),_chunk_size(chunk_size),_strategy(strategy)
//{
//#ifdef DEBUG_FTCHUNK
//	std::cerr << "ChunkMap:: loading availability map of size " << map.size() << ", chunk_size=" << chunk_size << ", chunknumber = " << chunk_number << std::endl ;
//#endif
//
//	_map.clear() ;
//	_map.resize(chunk_number) ;
//	_total_downloaded = 0 ;
//
//	for(uint32_t i=0;i<_map.size();++i)
//	{
//		uint32_t j = i & 31 ;	// i%32
//		uint32_t k = i >> 5 ;	// i/32
//
//		_map[i] = ( (map[k] & (1<<j)) > 0)?(FileChunksInfo::CHUNK_DONE) : (FileChunksInfo::CHUNK_OUTSTANDING) ;
//
//		if(_map[i] == FileChunksInfo::CHUNK_DONE)
//			_total_downloaded += _chunk_size ;
//	}
//}


void ChunkMap::dataReceived(const ftChunk::ChunkId& cid)
{
	// 1 - find which chunk contains the received data.
	//

	// trick: cid is the chunk offset. So we use it to get the chunk number.
	int n = (uint64_t)cid/_chunk_size ;

	std::map<ChunkNumber,ChunkDownloadInfo>::iterator itc(_slices_to_download.find(n)) ;

	if(itc == _slices_to_download.end()) 
	{
		std::cerr << "!!! ChunkMap::dataReceived: error: ChunkId " << cid << " corresponds to chunk number " << n << ", which is not being downloaded!" << std::endl ;
		assert(false) ;
		return ;
	}

	std::map<ftChunk::ChunkId,uint32_t>::iterator it(itc->second._slices.find(cid)) ;

	if(it == itc->second._slices.end()) 
	{
		std::cerr << "!!! ChunkMap::dataReceived: chunk " << cid << " is not found in slice lst of chunk number " << n << std::endl ;
		assert(false) ;
		return ;
	}

	_total_downloaded += it->second ;
	itc->second._remains -= it->second ;
	itc->second._slices.erase(it) ;

#ifdef DEBUG_FTCHUNK
	std::cerr << "*** ChunkMap::dataReceived: received data chunk " << cid << " for chunk number " << n << ", local remains=" << itc->second._remains << ", total downloaded=" << _total_downloaded << ", remains=" << _file_size - _total_downloaded << std::endl ;
#endif
	if(itc->second._remains == 0) // the chunk was completely downloaded
	{
#ifdef DEBUG_FTCHUNK
		std::cerr << "*** ChunkMap::dataReceived: Chunk is complete. Removing it." << std::endl ;
#endif
		_map[n] = FileChunksInfo::CHUNK_DONE ;
		_slices_to_download.erase(itc) ;
	}
}

// Warning: a chunk may be empty, but still being downloaded, so asking new slices from it
// will produce slices of size 0. This happens at the end of each chunk.
// --> I need to get slices from the next chunk, in such a case.
// --> solution:
// 	- have too chunks maps:
// 		1 indexed by peer id to feed the getChunk method
// 			- chunks pushed when new chunks are needed
// 			- chunks removed when empty
// 		1 indexed by chunk id to account for chunks being downloaded
// 			- chunks pushed when new chunks are needed
// 			- chunks removed when completely downloaded
//
bool ChunkMap::getDataChunk(const std::string& peer_id,uint32_t size_hint,ftChunk& chunk,bool& source_chunk_map_needed)
{
#ifdef DEBUG_FTCHUNK
	std::cerr << "*** ChunkMap::getDataChunk: size_hint = " << size_hint << std::endl ;
#endif
	// 1 - find if this peer already has an active chunk.
	//
	std::map<std::string,Chunk>::iterator it = _active_chunks_feed.find(peer_id) ;

	if(it == _active_chunks_feed.end())		
	{
		// 1 - select an available chunk id for this peer.
		//
		uint32_t c ;

		switch(_strategy)
		{
			case FileChunksInfo::CHUNK_STRATEGY_STREAMING:	c = getAvailableChunk(0,peer_id,source_chunk_map_needed) ;	// very bold!!
																			break ;

			case FileChunksInfo::CHUNK_STRATEGY_RANDOM: 		c = getAvailableChunk(rand()%_map.size(),peer_id,source_chunk_map_needed) ;
																			break ;
			default:
#ifdef DEBUG_FTCHUNK
													std::cerr << "!!! ChunkMap::getDataChunk: error!: unknown strategy" << std::endl ;
#endif
													return false ;
		}

		if(c >= _map.size()) 
			return false ;

		// 2 - add the chunk in the list of active chunks, and mark it as being downloaded
		//
		uint32_t soc = sizeOfChunk(c) ;
		_active_chunks_feed[peer_id] = Chunk( c*(uint64_t)_chunk_size, soc ) ;
		_map[c] = FileChunksInfo::CHUNK_ACTIVE ;
		_slices_to_download[c]._remains = soc ;			// init the list of slices to download
#ifdef DEBUG_FTCHUNK
		std::cout << "*** ChunkMap::getDataChunk: Allocating new chunk " << c << " for peer " << peer_id << std::endl ;
#endif
	}
#ifdef DEBUG_FTCHUNK
	else
		std::cout << "*** ChunkMap::getDataChunk: Re-using chunk " << it->second._start/_chunk_size << " for peer " << peer_id << std::endl ;
#endif

	// Get the first slice of the chunk, that is at most of length size
	//
	_active_chunks_feed[peer_id].getSlice(size_hint,chunk) ;
	_slices_to_download[chunk.offset/_chunk_size]._slices[chunk.id] = chunk.size ;

	if(_active_chunks_feed[peer_id].empty())
		_active_chunks_feed.erase(_active_chunks_feed.find(peer_id)) ;
	
#ifdef DEBUG_FTCHUNK
	std::cout << "*** ChunkMap::getDataChunk: returning slice " << chunk << " for peer " << peer_id << std::endl ;
#endif
	return true ;
}

bool ChunkMap::isChunkAvailable(uint64_t offset, uint32_t chunk_size) const 
{
	uint32_t chunk_number_start = offset/(uint64_t)_chunk_size ;
	uint32_t chunk_number_end = (offset+(uint64_t)chunk_size)/(uint64_t)_chunk_size ;

	if((offset+(uint64_t)chunk_size) % (uint64_t)_chunk_size == 0)
		--chunk_number_end ;

	// It's possible that chunk_number_start==chunk_number_end+1, but for this we need to have
	// chunk_size=0, and offset%_chunk_size=0, so the response "true" is still valid.
	//
	for(uint32_t i=chunk_number_start;i!=chunk_number_end;++i)
		if(_map[i] != FileChunksInfo::CHUNK_DONE)
			return false ;

	return true ;
}

void ChunkMap::setPeerAvailabilityMap(const std::string& peer_id,const CompressedChunkMap& cmap)
{
#ifdef DEBUG_FTCHUNK
	std::cout << "ChunkMap::Receiving new availability map for peer " << peer_id << std::endl ;
#endif

	if(cmap._map.size() != _map.size()/32+(_map.size()%32 != 0))
	{
		std::cerr << "ChunkMap::setPeerAvailabilityMap: chunk size / number of chunks is not correct. Dropping the info. cmap.size()=" << cmap._map.size() << ", _map/32+0/1 = " << _map.size()/32+(_map.size()%32 != 0) << std::endl ;
		return ;
	}

	// sets the map.
	//
	SourceChunksInfo& mi(_peers_chunks_availability[peer_id]) ;
	mi.cmap = cmap ;
	mi.TS = time(NULL) ;
	mi.is_full = true ;

	// Checks wether the map is full of not.
	//
	for(uint i=0;i<_map.size();++i)
		if(!cmap[i])
		{
			mi.is_full = false ;
			break ;
		}

#ifdef DEBUG_FTCHUNK
	std::cerr << "ChunkMap::setPeerAvailabilityMap: Setting chunk availability info for peer " << peer_id << std::endl ;
#endif
}

uint32_t ChunkMap::sizeOfChunk(uint32_t cid) const
{
	if(cid == _map.size()-1)
		return _file_size - (_map.size()-1)*_chunk_size ;
	else
		return _chunk_size ;
}

uint32_t ChunkMap::getAvailableChunk(uint32_t start_location,const std::string& peer_id,bool& map_is_too_old) 
{
	// Very bold algorithm: checks for 1st availabe chunk for this peer starting
	// from the given start location.
	std::map<std::string,SourceChunksInfo>::iterator it(_peers_chunks_availability.find(peer_id)) ;

	// Do we have records for this file source ?
	//
	if(it == _peers_chunks_availability.end())
	{
		SourceChunksInfo& pchunks(_peers_chunks_availability[peer_id]) ;

		// Ok, we don't have the info, so two cases:
		// 	- we are the actual source, so we can safely init the map to a full map
		// 	- we are not the source, so we init it with an empty map, and set the time stamp to 0.
		//
		if(peer_id == rsPeers->getOwnId())
		{
			pchunks.cmap._map.resize( CompressedChunkMap::getCompressedSize(_map.size()),~(uint32_t)0 ) ;
			pchunks.TS = 0 ;
			pchunks.is_full = true ;
		}
		else
		{
			pchunks.cmap._map.resize( CompressedChunkMap::getCompressedSize(_map.size()),0 ) ;
			pchunks.TS = 0 ;
			pchunks.is_full = false ;
		}

		it = _peers_chunks_availability.find(peer_id) ;
	}
	SourceChunksInfo& peer_chunks(it->second) ;

	// If the info is too old, we ask for a new one. When the map is full, we ask 10 times less, as it's probably not 
	// useful to get a new map that will also be full, but because we need to be careful not to mislead information,
	// we still keep asking.
	//
	time_t now = time(NULL) ;
	map_is_too_old = (int)now - (int)peer_chunks.TS > (int)SOURCE_CHUNK_MAP_UPDATE_PERIOD*(1+9*peer_chunks.is_full) ;

	// We will re-ask but not now seconds.
	peer_chunks.TS = now ;

	for(unsigned int i=0;i<_map.size();++i)
	{
		uint32_t j = (start_location+i)%(int)_map.size() ;	// index of the chunk

		if(_map[j] == FileChunksInfo::CHUNK_OUTSTANDING && peer_chunks.cmap[j])
		{
#ifdef DEBUG_FTCHUNK
			std::cerr << "ChunkMap::getAvailableChunk: returning chunk " << j << " for peer " << peer_id << std::endl;
#endif
			return j ;
		}
	}

#ifdef DEBUG_FTCHUNK
	std::cout << "!!! ChunkMap::getAvailableChunk: No available chunk from peer " << peer_id << ": returning false" << std::endl ;
#endif
	return _map.size() ;
}

void ChunkMap::getChunksInfo(FileChunksInfo& info) const 
{
	info.file_size = _file_size ;
	info.chunk_size = _chunk_size ;
	info.chunks = _map ;

	info.active_chunks.clear() ;

	for(std::map<ChunkNumber,ChunkDownloadInfo>::const_iterator it(_slices_to_download.begin());it!=_slices_to_download.end();++it)
		info.active_chunks.push_back(std::pair<uint32_t,uint32_t>(it->first,it->second._remains)) ;

	info.compressed_peer_availability_maps.clear() ;

	for(std::map<std::string,SourceChunksInfo>::const_iterator it(_peers_chunks_availability.begin());it!=_peers_chunks_availability.end();++it)
		info.compressed_peer_availability_maps[it->first] = it->second.cmap ;
}

void ChunkMap::getAvailabilityMap(CompressedChunkMap& compressed_map) const 
{
	compressed_map = CompressedChunkMap(_map) ; 

#ifdef DEBUG_FTCHUNK
	std::cerr << "ChunkMap:: retrieved availability map of size " << _map.size() << ", chunk_size=" << _chunk_size << std::endl ;
#endif
}

void ChunkMap::buildPlainMap(uint64_t size, CompressedChunkMap& map)
{
	uint32_t chunk_size(1024*1024) ;	// 1MB chunks
	uint64_t n = size/(uint64_t)chunk_size ;

	if(size % (uint64_t)chunk_size != 0)
		++n ;

	map = CompressedChunkMap(n,~uint32_t(0)) ;
}




