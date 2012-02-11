
/*
 * RetroShare C++ Interface.
 *
 * Copyright 2012-2012 by Robert Fernie.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License Version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA.
 *
 * Please report all bugs and problems to "retroshare@lunamutt.com".
 *
 */


#include <retroshare/rsmsgs.h>
#include <retroshare/rspeers.h>
#include "util/folderiterator.h"
#include "util/rsdir.h"
#include "util/rsstring.h"

#include "introserver.h"

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include <fstream>
#include <iostream>
                      
#define RSIS_CHECKTIME	60
#define MAX_PEER_AGE	(3600 * 24 * 14) // 2 weeks.
//#define MAX_PEER_AGE	(600) // 10 minutes - for testing

RsIntroServer::RsIntroServer()
	:mStartTS(0)
{
	std::cerr << "RsIntroServer::RsIntroServer()";
	std::cerr << std::endl;

	mStartTS = time(NULL);
	mLastCheck = 0;
	mCertCheckTime = 0;

	mMaxPeerAge = MAX_PEER_AGE;

	setupChatLobbies();

	mPeersFile = "./STORAGE/peers.txt";
	mCertLoadPath = "./NEWCERTS";
	mStorePeers = new RsIntroStore(mPeersFile);
	mStorePeers->loadPeers();
}



int RsIntroServer::tick()
{
	time_t now = time(NULL);

	if (mLastCheck + RSIS_CHECKTIME < now )
	{
		std::cerr << "RsIntroServer::tick() Check Cycle";
		std::cerr << std::endl;

		checkForNewCerts();

		cleanOldPeers();

		displayPeers();
	
		mLastCheck = now;
	}

	return 0;
}




int RsIntroServer::setupChatLobbies()
{
	std::cerr << "RsIntroServer::setupChatLobbies()";
	std::cerr << std::endl;

	std::string englishLobbyName = "Meeting Place (English)";
	std::string germanLobbyName = "Meeting Place (German)";
	std::string frenchLobbyName = "Meeting Place (French)";

	std::list<std::string> emptyList;
	uint32_t lobby_privacy_type = RS_CHAT_LOBBY_PRIVACY_LEVEL_PUBLIC;
	
	mEnglishLobby = rsMsgs->createChatLobby(englishLobbyName, emptyList, lobby_privacy_type);
	mFrenchLobby = rsMsgs->createChatLobby(frenchLobbyName, emptyList, lobby_privacy_type);
	mGermanLobby = rsMsgs->createChatLobby(germanLobbyName, emptyList, lobby_privacy_type);

	return 1;
}

int RsIntroServer::addCertificateFile(std::string filepath)
{
	std::cerr << "RsIntroServer::addCertificateFile(" << filepath << ")";
	std::cerr << std::endl;

	std::string certGPG;


	FILE *fd = fopen(filepath.c_str(), "r");
	if (!fd)
	{
		std::cerr << "RsIntroServer::addCertificateFile(" << filepath << ") FAILED to open file";
		std::cerr << std::endl;

		return 0;
	}

#define MAX_BUFFER  10240
	char BUFFER[MAX_BUFFER];

	while(BUFFER == fgets(BUFFER, MAX_BUFFER, fd))
	{
		certGPG += BUFFER;
	}

	std::cerr << "Read in Certificate: " << std::endl;
	std::cerr << "==========================================================" << std::endl;
	std::cerr << certGPG;
	std::cerr << std::endl;
	std::cerr << "==========================================================" << std::endl;

	return addNewUser(certGPG);
}


int RsIntroServer::addNewUser(std::string certGPG)
{
	std::cerr << "RsIntroServer::addNewUser()";
	std::cerr << std::endl;

	RsPeerDetails pd;
	std::string error_string;

	bool ok = rsPeers->loadDetailsFromStringCert(certGPG, pd,error_string);

	if (ok)
	{
		std::string gpgId = pd.gpg_id;
		std::string sslId;
		if (!pd.isOnlyGPGdetail)
		{
			sslId = pd.id;
		}
	
		std::cerr << "RsIntroServer::addNewUser() GPGID: " << gpgId << " SSLID: " << sslId;
		std::cerr << std::endl;

		rsPeers->addFriend(sslId, gpgId);

		/* add to internal storage */
		mStorePeers->addPeer(pd);
		mStorePeers->savePeers();
	}
	else
	{

		std::cerr << "RsIntroServer::addNewUser() Load Failed!";
		std::cerr << std::endl;

		return 0;
	}

	return 1;
}


int RsIntroServer::checkForNewCerts()
{
	std::cerr << "RsIntroServer::checkForNewCerts()";
	std::cerr << std::endl;

	/* check for new files */
	librs::util::FolderIterator dirIt(mCertLoadPath);
	if (!dirIt.isValid())
	{
		std::cerr << "RsIntroServer::checkForNewCerts()";
		std::cerr << " Missing Dir: " << mCertLoadPath << std::endl;
#ifdef RSIS_DEBUG
#endif
		return false;
	}

	std::list<std::string> newCertFiles;
	std::list<std::string>::iterator it;

	time_t now = time(NULL);
	time_t oldCheckTime = mCertCheckTime;
	mCertCheckTime = now;
        struct stat64 buf;

	while(dirIt.readdir())
	{
		/* check entry type */
		std::string fname;
		dirIt.d_name(fname);
		std::string fullname = mCertLoadPath + "/" + fname;
#ifdef RSIS_DEBUG
		std::cerr << "calling stats on " << fullname <<std::endl;
#endif
		
#ifdef WINDOWS_SYS
		std::wstring wfullname;
		librs::util::ConvertUtf8ToUtf16(fullname, wfullname);
		if (-1 != _wstati64(wfullname.c_str(), &buf))
#else
		if (-1 != stat64(fullname.c_str(), &buf))
#endif
		{
#ifdef RSIS_DEBUG
			std::cerr << "buf.st_mode: " << buf.st_mode <<std::endl;
#endif
			if (S_ISDIR(buf.st_mode))
			{
				/* ignore */
#ifdef RSIS_DEBUG
			std::cerr << "Ignoring Directory" << std::endl;
#endif


			}
			else if (S_ISREG(buf.st_mode))
			{
				/* check timestamp, push into queue */
				if ((buf.st_mtime > oldCheckTime) && (buf.st_size > MIN_CERT_SIZE))
				{
#ifdef RSIS_DEBUG
					std::cerr << "Adding New File: " << fullname << std::endl;
#endif
					newCertFiles.push_back(fullname);
				}
				else
				{
#ifdef RSIS_DEBUG
					std::cerr << "Ignoring Old File: " << fullname << std::endl;
					std::cerr << "\t it is: " << now - buf.st_mtime << " secs old" << std::endl;
#endif
				}

			}
		}
	}


	for(it = newCertFiles.begin(); it != newCertFiles.end(); it++)
	{
		std::cerr << "RsIntroServer::checkForNewCerts() adding: " << *it;
		std::cerr <<  std::endl;

		/* add Certificate */
		addCertificateFile(*it);
		sleep(1);

		std::cerr << "RsIntroServer::checkForNewCerts() removing: " << *it;
		std::cerr <<  std::endl;
		remove(it->c_str());
	}

	return 1;
}


int RsIntroServer::cleanOldPeers()
{
	std::cerr << "RsIntroServer::cleanOldPeers()";
	std::cerr << std::endl;

	std::list<std::string> oldIds;
	std::list<std::string>::iterator it;

	time_t now = time(NULL);
	time_t before = now - mMaxPeerAge;

	mStorePeers->getPeersBeforeTS(before, oldIds);

	for(it = oldIds.begin(); it != oldIds.end(); it++)
	{
		std::cerr << "RsIntroServer::cleanOldPeers() Removing: " << *it;
		std::cerr << std::endl;

		rsPeers->removeFriend(*it);
		mStorePeers->removePeer(*it);
	}

	mStorePeers->savePeers();

	return 1;
}



int RsIntroServer::displayPeers()
{
	std::cerr << "RsIntroServer::displayPeers()";
	std::cerr << std::endl;

	std::list<std::string> ids;
	std::list<std::string>::iterator it;

	rsPeers->getGPGAcceptedList(ids);

	for(it = ids.begin(); it != ids.end(); it++)
	{
		std::cerr << "GPGID: " << *it;

		if (mStorePeers->isValid(*it))
		{
			//std::cerr << " RsIS Flags: " << flags;
		}
		else
		{
			std::cerr << " Warning Unknown by RsIS ";
		}
	
		std::cerr << " => SSLIDS: ";
		std::list<std::string> sslIds;
		std::list<std::string>::iterator sit;
		rsPeers->getAssociatedSSLIds(*it, sslIds);

		for(sit = sslIds.begin(); sit != sslIds.end(); sit++)
		{
			std::cerr << *sit;
			if (rsPeers->isOnline(*sit))
			{
				std::cerr << " (ONLINE!), ";
			}
			else
			{
				std::cerr << " (Offline), ";
			}
		}
		std::cerr << std::endl;
	}
	return 1;
}



/*****************************************************************************************/


RsIntroStore::RsIntroStore(std::string storefile)
{
	std::cerr << "RsIntroStore::RsIntroStore(" << storefile << ")";
	std::cerr << std::endl;

	mStoreFile = storefile;
	mTempStoreFile = storefile + ".tmp";
}


bool	RsIntroStore::addPeer(const RsPeerDetails &pd)
{
	std::cerr << "RsIntroStore::addPeer()";
	std::cerr << std::endl;

	std::string gpgId = pd.gpg_id;

        std::map<std::string, storeData>::iterator it;
	it = mGpgData.find(gpgId);
	if (it != mGpgData.end())
	{
		std::cerr << "RsIntroStore::addPeer() ERROR peer already exists!";
		std::cerr << std::endl;

		return false;
	}

	storeData sd;
	sd.mAdded = time(NULL);
	sd.mFlags = 0;

	mGpgData[gpgId] = sd;

	return true;	
}


bool	RsIntroStore::removePeer(std::string gpgId)
{
	std::cerr << "RsIntroStore::removePeer()";
	std::cerr << std::endl;

        std::map<std::string, storeData>::iterator it;
	it = mGpgData.find(gpgId);
	if (it != mGpgData.end())
	{
		mGpgData.erase(it);
		return true;
	}
	return false;
}

bool    RsIntroStore::isValid(std::string gpgId)
{
        std::map<std::string, storeData>::iterator it;
	it = mGpgData.find(gpgId);
	return (it != mGpgData.end());
}

uint32_t RsIntroStore::getFlags(std::string gpgId)
{
	std::cerr << "RsIntroStore::removePeer()";
	std::cerr << std::endl;

        std::map<std::string, storeData>::iterator it;
	it = mGpgData.find(gpgId);
	if (it != mGpgData.end())
	{
		return it->second.mFlags;
	}
	return 0;
}




bool    RsIntroStore::getPeersBeforeTS(time_t ts, std::list<std::string> &plist)
{
	std::cerr << "RsIntroStore::getPeersBeforeTS()";
	std::cerr << std::endl;

        std::map<std::string, storeData>::iterator it;
	for(it = mGpgData.begin(); it != mGpgData.end(); it++)
	{
		std::cerr << "\t Checking: " << it->first << " Added: " << it->second.mAdded << "  vs TS: " << ts;
		std::cerr << std::endl;

		if (it->second.mAdded < ts)
		{
			std::cerr << "YES Adding OLD: " << it->first;
			std::cerr << std::endl;

			plist.push_back(it->first);
		}
	}
	return true;
}


bool	RsIntroStore::savePeers()
{
	std::cerr << "RsIntroStore::savePeers()";
	std::cerr << std::endl;

	FILE *fd = fopen(mTempStoreFile.c_str(), "w");
	if (!fd)
	{
		std::cerr << "RsIntroStore::savePeers() Cannot open tmp file";
		std::cerr << std::endl;

		return false;
	}

        std::map<std::string, storeData>::iterator it;

	for(it = mGpgData.begin(); it != mGpgData.end(); it++)
	{
		fprintf(fd, "%s %d %u\n", it->first.c_str(), (int) it->second.mAdded, it->second.mFlags);
	}
	fclose(fd);

	/* now move to final location */
	RsDirUtil::renameFile(mTempStoreFile,mStoreFile);
	return true;	
}

bool	RsIntroStore::loadPeers()
{
	std::cerr << "RsIntroStore::loadPeers()";
	std::cerr << std::endl;

	/* read from the file */
	FILE *fd = fopen(mStoreFile.c_str(), "r");
	if (!fd)
	{
		std::cerr << "RsIntroStore::loadPeers() Cannot open file, trying tmp";
		std::cerr << std::endl;

		FILE *fd = fopen(mTempStoreFile.c_str(), "r");
		if (!fd)
		{
			std::cerr << "RsIntroStore::loadPeers() Cannot open tmp file";
			std::cerr << std::endl;
			return false;
		}
	}


	char BUFFER[MAX_BUFFER];
	char certId[MAX_BUFFER];

	while(BUFFER == fgets(BUFFER, MAX_BUFFER, fd))
	{
		int addTs;
		uint32_t flags;
		if (3 == sscanf(BUFFER, "%s %d %u", certId, &addTs, &flags))
		{
			storeData sd;
			sd.mAdded = addTs;
			sd.mFlags = flags;

			std::string gpgId(certId);

			mGpgData[gpgId] = sd;

			std::cerr << "\t" << gpgId << " Added: " << addTs << " Flags: " << flags;
			std::cerr << std::endl;
		}
	}

	fclose(fd);
	return true;	
}


