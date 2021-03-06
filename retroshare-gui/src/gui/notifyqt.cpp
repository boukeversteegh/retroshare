/****************************************************************
 * This file is distributed under the following license:
 *
 * Copyright (c) 2010 RetroShare Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 *************************************************************************/

#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
//#include <QMutexLocker>
#include <QDesktopWidget>

#include "notifyqt.h"
#include <retroshare/rsnotify.h>
#include <retroshare/rspeers.h>
#include <util/rsdir.h>

#include "RsAutoUpdatePage.h"

#include "MainWindow.h"
#include "toaster/OnlineToaster.h"
#include "toaster/MessageToaster.h"
#include "toaster/DownloadToaster.h"
#include "toaster/ChatToaster.h"
#include "toaster/GroupChatToaster.h"
#include "toaster/ChatLobbyToaster.h"
#include "toaster/FriendRequestToaster.h"

#include "chat/ChatDialog.h"
#include "chat/ChatLobbyDialog.h"
#include "chat/ChatWidget.h"
#include "FriendsDialog.h"
#include "gui/settings/rsharesettings.h"
#include "SoundManager.h"

/*****
 * #define NOTIFY_DEBUG
 ****/

class Toaster
{
public:
	Toaster(QWidget *widget)
	{
		this->widget = widget;

		/* Values from settings */
		position = Settings->getToasterPosition();
		Settings->getToasterMargin();


		/* Standard values */
		timeToShow = 500;
		timeToLive = 3000;
		timeToHide = 500;

		/* Calculated values */
		elapsedTimeToShow = 0;
		elapsedTimeToLive = 0;
		elapsedTimeToHide = 0;
	}

public:
	QWidget *widget;

	/* Values from settings */
	RshareSettings::enumToasterPosition position;
	QPoint margin;

	/* Standard values */
	int timeToShow;
	int timeToLive;
	int timeToHide;

	/* Calculated values */
	QPoint startPos;
	QPoint endPos;
	int elapsedTimeToShow;
	int elapsedTimeToLive;
	int elapsedTimeToHide;
};

/*static*/ NotifyQt *NotifyQt::_instance = NULL;

/*static*/ NotifyQt *NotifyQt::Create ()
{
    if (_instance == NULL) {
        _instance = new NotifyQt ();
    }

    return _instance;
}

/*static*/ NotifyQt *NotifyQt::getInstance ()
{
    return _instance;
}

NotifyQt::NotifyQt() : cDialog(NULL)
{
	runningToasterTimer = new QTimer(this);
	connect(runningToasterTimer, SIGNAL(timeout()), this, SLOT(runningTick()));
	runningToasterTimer->setInterval(10); // tick 100 times a second
	runningToasterTimer->setSingleShot(true);
	{
		QMutexLocker m(&_mutex) ;
		_enabled = false ;
	}
}

void NotifyQt::notifyErrorMsg(int list, int type, std::string msg)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}
	emit errorOccurred(list,type,QString::fromUtf8(msg.c_str())) ;
}

void NotifyQt::notifyOwnAvatarChanged()
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

        #ifdef NOTIFY_DEBUG
	std::cerr << "Notifyqt:: notified that own avatar changed" << std::endl ;
        #endif
	emit ownAvatarChanged() ;
}

class SignatureEventData
{
	public:
		SignatureEventData(const void *_data,int32_t _len,unsigned int _signlen)
		{
			// We need a new memory chnk because there's no guarranty _sign nor _signlen are not in the stack

			sign = (unsigned char *)malloc(_signlen) ;
			signlen = new unsigned int ;
			*signlen = _signlen ;
			signature_result = 0 ;
			data = malloc(_len) ;
			len = _len ;
			memcpy(data,_data,len) ;
		}

		~SignatureEventData()
		{
			free(sign) ;
			delete signlen ;
			free(data) ;
		}

		void performSignature()
		{
			if(rsPeers->gpgSignData(data,len,sign,signlen))
				signature_result = 1 ;
		}

		void *data ;
		uint32_t len ;
		unsigned char *sign ;
		unsigned int *signlen ;
		int signature_result ;		// 0=pending, 1=done, 2=failed.
};

bool NotifyQt::askForDeferredSelfSignature(const void *data, const uint32_t len, unsigned char *sign, unsigned int *signlen,int& signature_result)
{
	{
		QMutexLocker m(&_mutex) ;

		std::cerr << "NotifyQt:: deferred signature event requeted. " << std::endl;

		// Look into the queue

		Sha1CheckSum chksum = RsDirUtil::sha1sum((uint8_t*)data,len) ;

		std::map<std::string,SignatureEventData*>::iterator it = _deferred_signature_queue.find(chksum.toStdString()) ;

		if(it != _deferred_signature_queue.end())
		{
			if(it->second->signature_result != 0)	// found it. Copy the result, and remove from the queue.
			{
				// We should check for the exact data match, for the sake of being totally secure.
				//
				std::cerr << "Found into queue: returning it" << std::endl;

				memcpy(sign,it->second->sign,*it->second->signlen) ;
				*signlen = *(it->second->signlen) ;
				signature_result = it->second->signature_result ;

				delete it->second ;
				_deferred_signature_queue.erase(it) ;

				return true ;
			}
			else
				return false ;		// already registered, but not done yet.
		}

		// Not found. Store in the queue and emit a signal.
		//
		std::cerr << "NotifyQt:: deferred signature event requeted. Pushing into queue" << std::endl;

		SignatureEventData *edta = new SignatureEventData(data,len,*signlen) ;

		_deferred_signature_queue[chksum.toStdString()] = edta ;
	}
	emit deferredSignatureHandlingRequested() ;
	return false;
}

void NotifyQt::handleSignatureEvent()
{
	std::cerr << "NotifyQt:: performing a deferred signature in the main GUI thread." << std::endl;

	static bool working = false ;

	if(!working)
	{
		working = true ;

	for(std::map<std::string,SignatureEventData*>::const_iterator it(_deferred_signature_queue.begin());it!=_deferred_signature_queue.end();++it)
		it->second->performSignature() ;

	working = false ;
	}
}



bool NotifyQt::askForPassword(const std::string& key_details, bool prev_is_bad, std::string& password)
{
	RsAutoUpdatePage::lockAllEvents() ;

	QInputDialog dialog;
	dialog.setWindowTitle(tr("PGP key passphrase"));
	dialog.setLabelText((prev_is_bad?tr("Wrong password !") + "\n\n" : QString()) +
						tr("Please enter the password to unlock your PGP key:") + "\n" + QString::fromUtf8(key_details.c_str()));
	dialog.setTextEchoMode(QLineEdit::Password);
	dialog.setWindowIcon(QIcon(":/images/rstray3.png"));
	dialog.setModal(true);

	int ret = dialog.exec();

	RsAutoUpdatePage::unlockAllEvents() ;

	if (ret == QDialog::Accepted) {
		 password = dialog.textValue().toUtf8().constData();
		 return true;
	}

	return false;
}
bool NotifyQt::askForPluginConfirmation(const std::string& plugin_file_name, const std::string& plugin_file_hash)
{
	RsAutoUpdatePage::lockAllEvents() ;

	QMessageBox dialog;
	dialog.setWindowTitle(tr("Unregistered plugin/executable"));

	QString text ;
	text += tr( "RetroShare has detected an unregistered plugin. This happens in two cases:<UL><LI>Your RetroShare executable has changed.</LI><LI>The plugin has changed</LI></UL>Click on Yes to authorize this plugin, or No to deny it. You can change your mind later in Options -> Plugins, then restart." ) ;
	text += "<UL>" ;
	text += "<LI>Hash:\t" + QString::fromStdString(plugin_file_hash) + "</LI>" ;
	text += "<LI>File:\t" + QString::fromStdString(plugin_file_name) + "</LI>";
	text += "</UL>" ;

	dialog.setText(text) ;
	dialog.setWindowIcon(QIcon(":/images/rstray3.png"));
	dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No) ;

	int ret = dialog.exec();

	RsAutoUpdatePage::unlockAllEvents() ;

	if (ret == QMessageBox::Yes) 
		return true ;
	else
		return false;
}

void NotifyQt::notifyDiscInfoChanged()
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

#ifdef NOTIFY_DEBUG
	std::cerr << "Notifyqt:: notified that discoveryInfo changed" << std::endl ;
#endif

	emit discInfoChanged() ;
}

void NotifyQt::notifyDownloadComplete(const std::string& fileHash)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

#ifdef NOTIFY_DEBUG
	std::cerr << "Notifyqt::notifyDownloadComplete notified that a download is completed" << std::endl;
#endif

	emit downloadComplete(QString::fromStdString(fileHash));
}

void NotifyQt::notifyDownloadCompleteCount(uint32_t count)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

	std::cerr << "Notifyqt::notifyDownloadCompleteCount " << count << std::endl;

	emit downloadCompleteCountChanged(count);
}

void NotifyQt::notifyDiskFull(uint32_t loc,uint32_t size_in_mb)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

	std::cerr << "Notifyqt:: notified that disk is full" << std::endl ;

	emit diskFull(loc,size_in_mb) ;
}

/* peer has changed the state */
void NotifyQt::notifyPeerStatusChanged(const std::string& peer_id, uint32_t state)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

#ifdef NOTIFY_DEBUG
	std::cerr << "Notifyqt:: notified that peer " << peer_id << " has changed the state to " << state << std::endl;
#endif

	emit peerStatusChanged(QString::fromStdString(peer_id), state);
}

/* one or more peers has changed the states */
void NotifyQt::notifyPeerStatusChangedSummary()
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

#ifdef NOTIFY_DEBUG
	std::cerr << "Notifyqt:: notified that one peer has changed the state" << std::endl;
#endif

	emit peerStatusChangedSummary();
}

void NotifyQt::notifyForumMsgReadSatusChanged(const std::string& forumId, const std::string& msgId, uint32_t status)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

	emit forumMsgReadSatusChanged(QString::fromStdString(forumId), QString::fromStdString(msgId), status);
}

void NotifyQt::notifyChannelMsgReadSatusChanged(const std::string& channelId, const std::string& msgId, uint32_t status)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

	emit channelMsgReadSatusChanged(QString::fromStdString(channelId), QString::fromStdString(msgId), status);
}

void NotifyQt::notifyOwnStatusMessageChanged()
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

#ifdef NOTIFY_DEBUG
	std::cerr << "Notifyqt:: notified that own avatar changed" << std::endl ;
#endif
	emit ownStatusMessageChanged() ;
}

void NotifyQt::notifyPeerHasNewAvatar(std::string peer_id)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}
#ifdef NOTIFY_DEBUG
	std::cerr << "notifyQt: notification of new avatar." << std::endl ;
#endif
	emit peerHasNewAvatar(QString::fromStdString(peer_id)) ;
}

void NotifyQt::notifyCustomState(const std::string& peer_id, const std::string& status_string)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

#ifdef NOTIFY_DEBUG
	std::cerr << "notifyQt: Received custom status string notification" << std::endl ;
#endif
	emit peerHasNewCustomStateString(QString::fromStdString(peer_id), QString::fromUtf8(status_string.c_str())) ;
}

void NotifyQt::notifyChatLobbyTimeShift(int shift)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

#ifdef NOTIFY_DEBUG
	std::cerr << "notifyQt: Received chat lobby time shift message: shift = " << shift << std::endl;
#endif
	emit chatLobbyTimeShift(shift) ;
}

void NotifyQt::handleChatLobbyTimeShift(int /*shift*/)
{
	static bool already = false ;

	if(!already)
	{
		already = true ;

		QString string = tr("For the chat lobbies to work properly, the time of your computer needs to be correct. Please check that this is the case (A possible time shift of several minutes was detected with your friends). ") ;

		QMessageBox::warning(NULL,tr("Please check your system clock."),string) ;
	}
}

void NotifyQt::notifyChatLobbyEvent(uint64_t lobby_id,uint32_t event_type,const std::string& nickname,const std::string& str) 
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

#ifdef NOTIFY_DEBUG
	std::cerr << "notifyQt: Received chat lobby event message: lobby #" << std::hex << lobby_id  << std::dec << ", event=" << event_type << ", str=\"" << str << "\"" << std::endl ;
#endif
	emit chatLobbyEvent(lobby_id,event_type,QString::fromUtf8(nickname.c_str()),QString::fromUtf8(str.c_str())) ;
}

void NotifyQt::notifyChatStatus(const std::string& peer_id,const std::string& status_string,bool is_private)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

#ifdef NOTIFY_DEBUG
	std::cerr << "notifyQt: Received chat status string: " << status_string << std::endl ;
#endif
	emit chatStatusChanged(QString::fromStdString(peer_id),QString::fromUtf8(status_string.c_str()),is_private) ;
}

void NotifyQt::notifyTurtleSearchResult(uint32_t search_id,const std::list<TurtleFileInfo>& files) 
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

#ifdef NOTIFY_DEBUG
	std::cerr << "in notify search result..." << std::endl ;
#endif

	for(std::list<TurtleFileInfo>::const_iterator it(files.begin());it!=files.end();++it)
	{
		FileDetail det ;
		det.rank = 0 ;
		det.age = 0 ;
		det.name = (*it).name ;
		det.hash = (*it).hash ;
		det.size = (*it).size ;
		det.id = "Anonymous" ;

		emit gotTurtleSearchResult(search_id,det) ;
	}
}

void NotifyQt::notifyHashingInfo(uint32_t type, const std::string& fileinfo)
{
	QString info;

	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

	switch (type) {
	case NOTIFY_HASHTYPE_EXAMINING_FILES:
		info = tr("Examining shared files...");
		break;
	case NOTIFY_HASHTYPE_FINISH:
		break;
	case NOTIFY_HASHTYPE_HASH_FILE:
		info = tr("Hashing file") + " " + QString::fromUtf8(fileinfo.c_str());
		break;
	case NOTIFY_HASHTYPE_SAVE_FILE_INDEX:
		info = tr("Saving file index...");
		break;
	}

	emit hashingInfoChanged(info);
}

void NotifyQt::notifyHistoryChanged(uint32_t msgId, int type)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

	emit historyChanged(msgId, type);
}

void NotifyQt::notifyListChange(int list, int type)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}
#ifdef NOTIFY_DEBUG
	std::cerr << "NotifyQt::notifyListChange()" << std::endl;
#endif
	switch(list)
	{
		case NOTIFY_LIST_NEIGHBOURS:
#ifdef NOTIFY_DEBUG
			std::cerr << "received neighbours changed" << std::endl ;
#endif
			emit neighboursChanged();
			break;
		case NOTIFY_LIST_FRIENDS:
#ifdef NOTIFY_DEBUG
			std::cerr << "received friends changed" << std::endl ;
#endif
			emit friendsChanged() ;
			break;
		case NOTIFY_LIST_DIRLIST_LOCAL:
#ifdef NOTIFY_DEBUG
			std::cerr << "received files changed" << std::endl ;
#endif
			emit filesPostModChanged(true) ;  /* Local */
			break;
		case NOTIFY_LIST_CHAT_LOBBY_INVITATION:
#ifdef NOTIFY_DEBUG
			std::cerr << "received files changed" << std::endl ;
#endif
			emit chatLobbyInviteReceived() ;  /* Local */
			break;
		case NOTIFY_LIST_DIRLIST_FRIENDS:
#ifdef NOTIFY_DEBUG
			std::cerr << "received files changed" << std::endl ;
#endif
			emit filesPostModChanged(false) ;  /* Local */
			break;
		case NOTIFY_LIST_SEARCHLIST:
			break;
		case NOTIFY_LIST_MESSAGELIST:
#ifdef NOTIFY_DEBUG
			std::cerr << "received msg changed" << std::endl ;
#endif
			emit messagesChanged() ;
			break;
		case NOTIFY_LIST_MESSAGE_TAGS:
#ifdef NOTIFY_DEBUG
			std::cerr << "received msg tags changed" << std::endl ;
#endif
			emit messagesTagsChanged();
			break;
		case NOTIFY_LIST_CHANNELLIST:
			break;
		case NOTIFY_LIST_TRANSFERLIST:
#ifdef NOTIFY_DEBUG
			std::cerr << "received transfer changed" << std::endl ;
#endif
			emit transfersChanged() ;
			break;
		case NOTIFY_LIST_CONFIG:
#ifdef NOTIFY_DEBUG
			std::cerr << "received config changed" << std::endl ;
#endif
			emit configChanged() ;
			break ;
		case NOTIFY_LIST_FORUMLIST_LOCKED:
#ifdef NOTIFY_DEBUG
			std::cerr << "received forum msg changed" << std::endl ;
#endif
			emit forumsChanged(); // use connect with Qt::QueuedConnection
			break;
		case NOTIFY_LIST_CHANNELLIST_LOCKED:
#ifdef NOTIFY_DEBUG
			std::cerr << "received channel msg changed" << std::endl ;
#endif
			emit channelsChanged(type); // use connect with Qt::QueuedConnection
			break;
		case NOTIFY_LIST_PUBLIC_CHAT:
#ifdef NOTIFY_DEBUG
			std::cerr << "received public chat changed" << std::endl ;
#endif
			emit publicChatChanged(type);
			break;
		case NOTIFY_LIST_PRIVATE_INCOMING_CHAT:
		case NOTIFY_LIST_PRIVATE_OUTGOING_CHAT:
#ifdef NOTIFY_DEBUG
			std::cerr << "received private chat changed" << std::endl ;
#endif
			emit privateChatChanged(list, type);
			break;
		case NOTIFY_LIST_CHAT_LOBBY_LIST:
#ifdef NOTIFY_DEBUG
			std::cerr << "received notify chat lobby list" << std::endl;
#endif
			emit lobbyListChanged();
			break;

		case NOTIFY_LIST_GROUPLIST:
#ifdef NOTIFY_DEBUG
			std::cerr << "received groups changed" << std::endl ;
#endif
			emit groupsChanged(type);
			break;
		default:
			break;
	}
	return;
}


void NotifyQt::notifyListPreChange(int list, int /*type*/)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

#ifdef NOTIFY_DEBUG
	std::cerr << "NotifyQt::notifyListPreChange()" << std::endl;
#endif
	switch(list)
	{
		case NOTIFY_LIST_NEIGHBOURS:
			break;
		case NOTIFY_LIST_FRIENDS:
			emit friendsChanged() ;
			break;
		case NOTIFY_LIST_DIRLIST_FRIENDS:
			emit filesPreModChanged(false) ;	/* remote */
			break ;
		case NOTIFY_LIST_DIRLIST_LOCAL:
			emit filesPreModChanged(true) ;	/* local */
			break;
		case NOTIFY_LIST_SEARCHLIST:
			break;
		case NOTIFY_LIST_MESSAGELIST:
			break;
		case NOTIFY_LIST_CHANNELLIST:
			break;
		case NOTIFY_LIST_TRANSFERLIST:
			break;
		default:
			break;
	}
	return;
}

	/* New Timer Based Update scheme ...
	 * means correct thread seperation
	 *
	 * uses Flags, to detect changes 
	 */

void NotifyQt::enable()
{
	QMutexLocker m(&_mutex) ;
	std::cerr << "Enabling notification system" << std::endl;
	_enabled = true ;
}

void NotifyQt::UpdateGUI()
{
	if(RsAutoUpdatePage::eventsLocked())
		return ;

	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

	static bool already_updated = false ;	// these only update once at start because they may already have been set before 
														// the gui is running, then they get updated by callbacks.
	if(!already_updated)
	{
		emit messagesChanged() ;
		emit neighboursChanged();
		emit configChanged();

		already_updated = true ;
	}
	
	/* Finally Check for PopupMessages / System Error Messages */

	if (rsNotify)
	{
		uint32_t sysid;
		uint32_t type;
		std::string title, id, msg;

		if (rsNotify->NotifyPopupMessage(type, id, title, msg))
		{
			uint popupflags = Settings->getNotifyFlags();

			/* You can set timeToShow, timeToLive and timeToHide or can leave the standard */
			Toaster *toaster = NULL;

			switch(type)
			{
				case RS_POPUP_MSG:
					soundManager->play(SOUND_MESSAGE_ARRIVED);

					if (popupflags & RS_POPUP_MSG)
					{
						toaster = new Toaster(new MessageToaster(id, QString::fromUtf8(title.c_str()), QString::fromUtf8(msg.c_str())));
					}
					break;
				case RS_POPUP_CONNECT:
					soundManager->play(SOUND_USER_ONLINE);

					if (popupflags & RS_POPUP_CONNECT)
					{
						toaster = new Toaster(new OnlineToaster(id));
					}
					break;
				case RS_POPUP_DOWNLOAD:
					soundManager->play(SOUND_DOWNLOAD_COMPLETE);

					if (popupflags & RS_POPUP_DOWNLOAD)
					{
						/* id = file hash */
						toaster = new Toaster(new DownloadToaster(id, QString::fromUtf8(title.c_str())));
					}
					break;
				case RS_POPUP_CHAT:
					if (popupflags & RS_POPUP_CHAT)
					{
						ChatDialog *chatDialog = ChatDialog::getChat(id, 0);
						ChatWidget *chatWidget;
						if (chatDialog && (chatWidget = chatDialog->getChatWidget()) && chatWidget->isActive()) {
							// do not show when active
							break;
						}
						toaster = new Toaster(new ChatToaster(id, QString::fromUtf8(msg.c_str())));
					}
					break;
				case RS_POPUP_GROUPCHAT:
					if (popupflags & RS_POPUP_GROUPCHAT)
					{
						MainWindow *mainWindow = MainWindow::getInstance();
						if (mainWindow && mainWindow->isActiveWindow() && !mainWindow->isMinimized()) {
							if (MainWindow::getActivatePage() == MainWindow::Friends) {
								if (FriendsDialog::isGroupChatActive()) {
									// do not show when active
									break;
								}
							}
						}
						toaster = new Toaster(new GroupChatToaster(id, QString::fromUtf8(msg.c_str())));
					}
					break;
				case RS_POPUP_CHATLOBBY:
					if (popupflags & RS_POPUP_CHATLOBBY)
					{
						ChatDialog *chatDialog = ChatDialog::getChat(id, 0);
						ChatWidget *chatWidget;
						if (chatDialog && (chatWidget = chatDialog->getChatWidget()) && chatWidget->isActive()) {
							// do not show when active
							break;
						}
						ChatLobbyDialog *chatLobbyDialog = dynamic_cast<ChatLobbyDialog*>(chatDialog);
						if (!chatLobbyDialog || chatLobbyDialog->isParticipantMuted(QString::fromUtf8(title.c_str()))) {
							// participant is muted
							break;
						}
						toaster = new Toaster(new ChatLobbyToaster(id, QString::fromUtf8(title.c_str()), QString::fromUtf8(msg.c_str())));
					}
					break;
				case RS_POPUP_CONNECT_ATTEMPT:
					if (popupflags & RS_POPUP_CONNECT_ATTEMPT)
					{
						// id = gpgid
						// title = ssl name
						// msg = peer id
						toaster = new Toaster(new FriendRequestToaster(id, QString::fromUtf8(title.c_str()), msg));
					}
					break;
			}

			if (toaster) {
				/* init attributes */
				toaster->widget->setWindowFlags(Qt::ToolTip | Qt::WindowStaysOnTopHint);

				/* add toaster to waiting list */
//				QMutexLocker lock(&waitingToasterMutex);
				waitingToasterList.push_back(toaster);
			}
		}

		if (rsNotify->NotifySysMessage(sysid, type, title, msg))
		{
			/* make a warning message */
			switch(type)
			{
				case RS_SYS_ERROR:
					QMessageBox::critical(MainWindow::getInstance(),
							QString::fromUtf8(title.c_str()),
							QString::fromUtf8(msg.c_str()));
					break;
				case RS_SYS_WARNING:
					QMessageBox::warning(MainWindow::getInstance(),
							QString::fromUtf8(title.c_str()),
							QString::fromUtf8(msg.c_str()));
					break;
				default:
				case RS_SYS_INFO:
					QMessageBox::information(MainWindow::getInstance(),
							QString::fromUtf8(title.c_str()),
							QString::fromUtf8(msg.c_str()));
					break;
			}
		}

		if (rsNotify->NotifyLogMessage(sysid, type, title, msg))
		{
			/* make a log message */
			std::string logMesString = title + " " + msg;
			switch(type)
			{
				case RS_SYS_ERROR:
				case RS_SYS_WARNING:
				case RS_SYS_INFO:
					emit logInfoChanged(QString::fromUtf8(logMesString.c_str()));
			}
		}
	}

	/* Now start the waiting toasters */
	startWaitingToasters();
}

void NotifyQt::testToaster(uint notifyFlags, /*RshareSettings::enumToasterPosition*/ int position, QPoint margin)
{
	QString title = tr("Test");
	QString message = tr("This is a test.");

	std::string id = rsPeers->getOwnId();

	uint pos = 0;

	while (notifyFlags) {
		uint type = notifyFlags & (1 << pos);
		notifyFlags &= ~(1 << pos);
		++pos;

		Toaster *toaster = NULL;

		switch(type)
		{
			case RS_POPUP_MSG:
				toaster = new Toaster(new MessageToaster(id, title, message));
				break;
			case RS_POPUP_CONNECT:
				toaster = new Toaster(new OnlineToaster(id));
				break;
			case RS_POPUP_DOWNLOAD:
				toaster = new Toaster(new DownloadToaster(id, title));
				break;
			case RS_POPUP_CHAT:
				toaster = new Toaster(new ChatToaster(id, message));
				break;
			case RS_POPUP_GROUPCHAT:
				toaster = new Toaster(new GroupChatToaster(id, message));
				break;
			case RS_POPUP_CHATLOBBY:
				toaster = new Toaster(new ChatLobbyToaster(id, title, message));
				break;
			case RS_POPUP_CONNECT_ATTEMPT:
				toaster = new Toaster(new FriendRequestToaster(id, title, id));
				break;
		}

		if (toaster) {
			/* init attributes */
			toaster->widget->setWindowFlags(Qt::ToolTip | Qt::WindowStaysOnTopHint);
			toaster->position = (RshareSettings::enumToasterPosition) position;
			toaster->margin = margin;

			/* add toaster to waiting list */
//			QMutexLocker lock(&waitingToasterMutex);
			waitingToasterList.push_back(toaster);
		}
	}
}

void NotifyQt::notifyChatStyleChanged(int /*ChatStyle::enumStyleType*/ styleType)
{
	{
		QMutexLocker m(&_mutex) ;
		if(!_enabled)
			return ;
	}

	emit chatStyleChanged(styleType);
}

void NotifyQt::notifySettingsChanged()
{
	emit settingsChanged();
}

void NotifyQt::startWaitingToasters()
{
	{
//		QMutexLocker lock(&waitingToasterMutex);

		if (waitingToasterList.empty()) {
			/* No toasters are waiting */
			return;
		}
	}

	{
//		QMutexLocker lock(&runningToasterMutex);

		if (runningToasterList.size() >= 3) {
			/* Don't show more than 3 toasters at once */
			return;
		}
	}

	Toaster *toaster = NULL;

	{
//		QMutexLocker lock(&waitingToasterMutex);

		if (waitingToasterList.size()) {
			/* Take one toaster of the waiting list */
			toaster = waitingToasterList.front();
			waitingToasterList.pop_front();
		}
	}

	if (toaster) {
//		QMutexLocker lock(&runningToasterMutex);

		/* Calculate positions */
		QSize size = toaster->widget->size();

		QDesktopWidget *desktop = QApplication::desktop();
		QRect desktopGeometry = desktop->availableGeometry(desktop->primaryScreen());

		switch (toaster->position) {
		case RshareSettings::TOASTERPOS_TOPLEFT:
			toaster->startPos = QPoint(desktopGeometry.left() + toaster->margin.x(), desktopGeometry.top() - size.height());
			toaster->endPos = QPoint(toaster->startPos.x(), desktopGeometry.top() + toaster->margin.y());
			break;
		case RshareSettings::TOASTERPOS_TOPRIGHT:
			toaster->startPos = QPoint(desktopGeometry.right() - size.width() - toaster->margin.x(), desktopGeometry.top() - size.height());
			toaster->endPos = QPoint(toaster->startPos.x(), desktopGeometry.top() + toaster->margin.y());
			break;
		case RshareSettings::TOASTERPOS_BOTTOMLEFT:
			toaster->startPos = QPoint(desktopGeometry.left() + toaster->margin.x(), desktopGeometry.bottom());
			toaster->endPos = QPoint(toaster->startPos.x(), desktopGeometry.bottom() - size.height() - toaster->margin.y());
			break;
		case RshareSettings::TOASTERPOS_BOTTOMRIGHT: // default
		default:
			toaster->startPos = QPoint(desktopGeometry.right() - size.width() - toaster->margin.x(), desktopGeometry.bottom());
			toaster->endPos = QPoint(toaster->startPos.x(), desktopGeometry.bottom() - size.height() - toaster->margin.y());
			break;
		}

		/* Initialize widget */
		toaster->widget->move(toaster->startPos);

		/* Initialize toaster */
		toaster->elapsedTimeToShow = 0;
		toaster->elapsedTimeToLive = 0;
		toaster->elapsedTimeToHide = 0;

		/* Add toaster to the running list */
		runningToasterList.push_front(toaster);
		if (runningToasterTimer->isActive() == false) {
			/* Start the toaster timer */
			runningToasterTimer->start();
		}
	}
}

void NotifyQt::runningTick()
{
//	QMutexLocker lock(&runningToasterMutex);

	int interval = runningToasterTimer->interval();
	QPoint diff;

	QList<Toaster*>::iterator it = runningToasterList.begin();
	while (it != runningToasterList.end()) {
		Toaster *toaster = *it;

		bool visible = true;
		if (toaster->elapsedTimeToShow) {
			/* Toaster is started, check for visible */
			visible = toaster->widget->isVisible();
		}

		QPoint newPos;
		enum { NOTHING, SHOW, HIDE } operation = NOTHING;

		if (visible && toaster->elapsedTimeToShow <= toaster->timeToShow) {
			/* Toaster is showing */
			if (toaster->elapsedTimeToShow == 0) {
				/* Toaster is not visible, show it now */
				operation = SHOW;
			}

			toaster->elapsedTimeToShow += interval;

			newPos = QPoint(toaster->startPos.x() - (toaster->startPos.x() - toaster->endPos.x()) * toaster->elapsedTimeToShow / toaster->timeToShow,
							toaster->startPos.y() - (toaster->startPos.y() - toaster->endPos.y()) * toaster->elapsedTimeToShow / toaster->timeToShow);
		} else if (visible && toaster->elapsedTimeToLive <= toaster->timeToLive) {
			/* Toaster is living */
			toaster->elapsedTimeToLive += interval;

			newPos = toaster->endPos;
		} else if (visible && toaster->elapsedTimeToHide <= toaster->timeToHide) {
			/* Toaster is hiding */
			toaster->elapsedTimeToHide += interval;

			if (toaster->elapsedTimeToHide == toaster->timeToHide) {
				/* Toaster is back at the start position, hide it */
				operation = HIDE;
			}

			newPos = QPoint(toaster->startPos.x() - (toaster->startPos.x() - toaster->endPos.x()) * (toaster->timeToHide - toaster->elapsedTimeToHide) / toaster->timeToHide,
							toaster->startPos.y() - (toaster->startPos.y() - toaster->endPos.y()) * (toaster->timeToHide - toaster->elapsedTimeToHide) / toaster->timeToHide);
		} else {
			/* Toaster is hidden, delete it */
			it = runningToasterList.erase(it);
			delete(toaster->widget);
			delete(toaster);
			continue;
		}

		toaster->widget->move(newPos + diff);
		diff += newPos - toaster->startPos;

		QRect mask = QRect(0, 0, toaster->widget->width(), qAbs(toaster->startPos.y() - newPos.y()));
		if (newPos.y() > toaster->startPos.y()) {
			/* Toaster is moving from top */
			mask.moveTop(toaster->widget->height() - (newPos.y() - toaster->startPos.y()));
		}
		toaster->widget->setMask(QRegion(mask));

		switch (operation) {
		case NOTHING:
			break;
		case SHOW:
			toaster->widget->show();
			break;
		case HIDE:
			toaster->widget->hide();
			break;
		}

		++it;
	}

	if (runningToasterList.size()) {
		/* There are more running toasters, start the timer again */
		runningToasterTimer->start();
	}
}

//void NotifyQt::displaySearch()
//{
//	iface->lockData(); /* Lock Interface */
//
//#ifdef NOTIFY_DEBUG
//	std::ostringstream out;
//	std::cerr << out.str();
//#endif
//
//	iface->unlockData(); /* UnLock Interface */
//}

//  void NotifyQt::displayChat()
//  {
//  	iface->lockData(); /* Lock Interface */
//  
//  #ifdef NOTIFY_DEBUG
//  	std::ostringstream out;
//  	std::cerr << out.str();
//  #endif
//  
//  	iface->unlockData(); /* UnLock Interface */
//  
//  	if (hDialog)
//   		hDialog -> insertChat();
//  }
//
//
//void NotifyQt::displayChannels()
//{
//	iface->lockData(); /* Lock Interface */
//
//#ifdef NOTIFY_DEBUG
//	std::ostringstream out;
//	std::cerr << out.str();
//#endif
//
//	iface->unlockData(); /* UnLock Interface */
//
//	if (sDialog)
// 		sDialog -> insertChannels();
//}
//
//
//void NotifyQt::displayTransfers()
//{
//	/* Do the GUI */
//	if (tDialog)
//		tDialog->insertTransfers();
//}
//
//
//void NotifyQt::preDisplayNeighbours()
//{
//
//}
//
//void NotifyQt::preDisplayFriends()
//{
//
//}

//void NotifyQt::preDisplaySearch()
//{
//
//}
//
//void NotifyQt::preDisplayMessages()
//{
//
//}
//
//void NotifyQt::preDisplayChannels()
//{
//
//}
//
//void NotifyQt::preDisplayTransfers()
//{
//
//
//}
