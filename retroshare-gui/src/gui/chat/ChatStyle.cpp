/****************************************************************
 *
 *  RetroShare is distributed under the following license:
 *
 *  Copyright (C) 2006, Thunder
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 *  Boston, MA  02110-1301, USA.
 ****************************************************************/

#include <QApplication>
#include <QDesktopWidget>
#include <QWidget>
#include <QFile>
#include <QIcon>
#include <QPushButton>
#include <QXmlStreamReader>

#include <iostream>

#include "ChatStyle.h"
#include "gui/settings/rsharesettings.h"
#include "gui/notifyqt.h"

#include <retroshare/rsinit.h>

enum enumGetStyle
{
    GETSTYLE_INCOMING,
    GETSTYLE_OUTGOING,
    GETSTYLE_HINCOMING,
    GETSTYLE_HOUTGOING
};

/* Default constructor */
ChatStyle::ChatStyle() : QObject()
{
    m_styleType = TYPE_UNKNOWN;

    connect(NotifyQt::getInstance(), SIGNAL(chatStyleChanged(int)), SLOT(styleChanged(int)));
}

/* Destructor. */
ChatStyle::~ChatStyle()
{
}

void ChatStyle::styleChanged(int styleType)
{
    if (m_styleType == styleType) {
        setStyleFromSettings(m_styleType);
    }
}

bool ChatStyle::setStylePath(const QString &stylePath, const QString &styleVariant)
{
    m_styleType = TYPE_UNKNOWN;

    m_styleDir.setPath(QApplication::applicationDirPath());
    if (m_styleDir.cd(stylePath) == false) {
        m_styleDir = QDir("");
        m_styleVariant.clear();
        return false;
    }

    m_styleVariant = styleVariant;

    return true;
}

bool ChatStyle::setStyleFromSettings(enumStyleType styleType)
{
    QString stylePath;
    QString styleVariant;

    switch (styleType) {
    case TYPE_PUBLIC:
        Settings->getPublicChatStyle(stylePath, styleVariant);
        break;
    case TYPE_PRIVATE:
        Settings->getPrivateChatStyle(stylePath, styleVariant);
        break;
    case TYPE_HISTORY:
        Settings->getHistoryChatStyle(stylePath, styleVariant);
        break;
    case TYPE_UNKNOWN:
        return false;
    }

    bool result = setStylePath(stylePath, styleVariant);

    m_styleType = styleType;

    return result;
}

void ChatStyle::loadEmoticons()
{
    QString sm_codes;
    bool internalEmoticons = true;

#if defined(Q_OS_WIN32)
    // first try external emoticons
    QFile sm_file(QApplication::applicationDirPath() + "/emoticons/emotes.acs");
    if(sm_file.open(QIODevice::ReadOnly))
    {
        internalEmoticons = false;
    } else {
        // then embedded emotions
        sm_file.setFileName(":/smileys/emotes.acs");
        if(!sm_file.open(QIODevice::ReadOnly))
        {
            std::cout << "error opening ressource file" << std::endl ;
            return ;
        }
    }
#else
    QFile sm_file(QString(":/smileys/emotes.acs"));
    if(!sm_file.open(QIODevice::ReadOnly))
    {
        std::cout << "error opening ressource file" << std::endl ;
        return ;
    }
#endif

    sm_codes = sm_file.readAll();
    sm_file.close();

    sm_codes.remove("\n");
    sm_codes.remove("\r");

    int i = 0;
    QString smcode;
    QString smfile;
    while(sm_codes[i] != '{')
    {
        i++;
    }
    while (i < sm_codes.length()-2)
    {
        smcode = "";
        smfile = "";

        while(sm_codes[i] != '\"')
        {
            i++;
        }
        i++;
        while (sm_codes[i] != '\"')
        {
            smcode += sm_codes[i];
            i++;

        }
        i++;

        while(sm_codes[i] != '\"')
        {
            i++;
        }
        i++;
        while(sm_codes[i] != '\"' && sm_codes[i+1] != ';')
        {
            smfile += sm_codes[i];
            i++;
        }
        i++;
        if(!smcode.isEmpty() && !smfile.isEmpty()) {
            if (internalEmoticons) {
                smileys.insert(smcode, ":/"+smfile);
            } else {
                smileys.insert(smcode, smfile);
            }
        }
    }

    // init <img> embedder
    defEmbedImg.InitFromAwkwardHash(smileys);
}

void ChatStyle::showSmileyWidget(QWidget *parent, QWidget *button, const char *slotAddMethod)
{
    QWidget *smWidget = new QWidget(parent, Qt::Popup);

    smWidget->setAttribute( Qt::WA_DeleteOnClose);
    smWidget->setWindowTitle("Emoticons");
    smWidget->setWindowIcon(QIcon(QString(":/images/rstray3.png")));
    smWidget->setBaseSize(4*24, (smileys.size()/4)*24);

    //Warning: this part of code was taken from kadu instant messenger;
    //         It was EmoticonSelector::alignTo(QWidget* w) function there
    //         comments are Polish, I dont' know how does it work...
    // oblicz pozycj� widgetu do kt�rego r�wnamy
    // oblicz rozmiar selektora
    QPoint pos = button->mapToGlobal(QPoint(0,0));
    QSize e_size = smWidget->sizeHint();
    // oblicz rozmiar pulpitu
    QSize s_size = QApplication::desktop()->size();
    // oblicz dystanse od widgetu do lewego brzegu i do prawego
    int l_dist = pos.x();
    int r_dist = s_size.width() - (pos.x() + button->width());
    // oblicz pozycj� w zale�no�ci od tego czy po lewej stronie
    // jest wi�cej miejsca czy po prawej
    int x;
    if (l_dist >= r_dist)
        x = pos.x() - e_size.width();
    else
        x = pos.x() + button->width();
    // oblicz pozycj� y - centrujemy w pionie
    int y = pos.y() + button->height()/2 - e_size.height()/2;
    // je�li wychodzi poza doln� kraw�d� to r�wnamy do niej
    if (y + e_size.height() > s_size.height())
        y = s_size.height() - e_size.height();
    // je�li wychodzi poza g�rn� kraw�d� to r�wnamy do niej
    if (y < 0)
        y = 0;
    // ustawiamy selektor na wyliczonej pozycji
    smWidget->move(x, y);

    x = 0;
    y = 0;

    QHashIterator<QString, QString> i(smileys);
    while(i.hasNext())
    {
        i.next();
        QPushButton *smButton = new QPushButton("", smWidget);
        smButton->setGeometry(x*24, y*24, 24,24);
        smButton->setIconSize(QSize(24,24));
        smButton->setIcon(QPixmap(i.value()));
        smButton->setToolTip(i.key());
        ++x;
        if(x > 4)
        {
            x = 0;
            y++;
        }
        QObject::connect(smButton, SIGNAL(clicked()), parent, slotAddMethod);
        QObject::connect(smButton, SIGNAL(clicked()), smWidget, SLOT(close()));
    }

    smWidget->show();
}

static QString getStyle(QDir &styleDir, QString styleVariant, enumGetStyle type)
{
    QString style;

    if (styleDir == QDir("")) {
        return "";
    }

    QFile fileHtml;
    switch (type) {
    case GETSTYLE_INCOMING:
        fileHtml.setFileName(QFileInfo(styleDir, "incoming.htm").absoluteFilePath());
        break;
    case GETSTYLE_OUTGOING:
        fileHtml.setFileName(QFileInfo(styleDir, "outgoing.htm").absoluteFilePath());
        break;
    case GETSTYLE_HINCOMING:
        fileHtml.setFileName(QFileInfo(styleDir, "hincoming.htm").absoluteFilePath());
        break;
    case GETSTYLE_HOUTGOING:
        fileHtml.setFileName(QFileInfo(styleDir, "houtgoing.htm").absoluteFilePath());
        break;
    default:
        return "";
    }

    if (fileHtml.open(QIODevice::ReadOnly)) {
        style = fileHtml.readAll();
        fileHtml.close();

        QFile fileCss(QFileInfo(styleDir, "main.css").absoluteFilePath());
        QString css;
        if (fileCss.open(QIODevice::ReadOnly)) {
            css = fileCss.readAll();
            fileCss.close();
        }

        if (styleVariant.isEmpty() == false) {
            QFile fileCssVariant(QFileInfo(styleDir, "variants/" + styleVariant + ".css").absoluteFilePath());
            QString cssVariant;
            if (fileCssVariant.open(QIODevice::ReadOnly)) {
                cssVariant = fileCssVariant.readAll();
                fileCssVariant.close();

                css += "\n" + cssVariant;
            }
        }

        style.replace("%css-style%", css);
    }

    return style;
}

QString ChatStyle::formatText(QString &message, unsigned int flag)
{
    if (flag == 0) {
        // nothing to do
        return message;
    }

    QDomDocument doc;
    doc.setContent(message);

    QDomElement body = doc.documentElement();
    if (flag & CHAT_FORMATTEXT_EMBED_LINKS) {
        RsChat::embedHtml(doc, body, defEmbedAhref);
    }
    if (flag & CHAT_FORMATTEXT_EMBED_SMILEYS) {
        RsChat::embedHtml(doc, body, defEmbedImg);
    }

    return doc.toString(-1);		// -1 removes any annoying carriage return misinterpreted by QTextEdit
}

QString ChatStyle::formatMessage(enumFormatMessage type, QString &name, QDateTime &timestamp, QString &message, unsigned int flag)
{
    QString style;

    switch (type) {
    case FORMATMSG_INCOMING:
        style = getStyle(m_styleDir, m_styleVariant, GETSTYLE_INCOMING);
        break;
    case FORMATMSG_OUTGOING:
        style = getStyle(m_styleDir, m_styleVariant, GETSTYLE_OUTGOING);
        break;
    case FORMATMSG_HINCOMING:
        style = getStyle(m_styleDir, m_styleVariant, GETSTYLE_HINCOMING);
        break;
    case FORMATMSG_HOUTGOING:
        style = getStyle(m_styleDir, m_styleVariant, GETSTYLE_HOUTGOING);
        break;
    }

    if (style.isEmpty()) {
        // default style
        style = "<table width='100%'><tr><td><b>%name%</b></td><td width='130' align='right'>%timestamp%</td></tr></table><table width='100%'><tr><td>%message%</td></tr></table>";
    }

    unsigned int formatFlag = 0;
    if (flag & CHAT_FORMATMSG_EMBED_SMILEYS) {
        formatFlag |= CHAT_FORMATTEXT_EMBED_SMILEYS;
    }
    if (flag & CHAT_FORMATMSG_EMBED_LINKS) {
        formatFlag |= CHAT_FORMATTEXT_EMBED_LINKS;
    }

    QString msg = formatText(message, formatFlag);

//    //replace http://, https:// and www. with <a href> links
//    QRegExp rx("(retroshare://[^ <>]*)|(https?://[^ <>]*)|(www\\.[^ <>]*)");
//    int count = 0;
//    int pos = 100; //ignore the first 100 char because of the standard DTD ref
//    while ( (pos = rx.indexIn(message, pos)) != -1 ) {
//        //we need to look ahead to see if it's already a well formed link
//        if (message.mid(pos - 6, 6) != "href=\"" && message.mid(pos - 6, 6) != "href='" && message.mid(pos - 6, 6) != "ttp://" ) {
//            QString tempMessg = message.left(pos) + "<a href=\"" + rx.cap(count) + "\">" + rx.cap(count) + "</a>" + message.mid(pos + rx.matchedLength(), -1);
//            message = tempMessg;
//        }
//        pos += rx.matchedLength() + 15;
//        count ++;
//    }

//    {
//	QHashIterator<QString, QString> i(smileys);
//	while(i.hasNext())
//	{
//            i.next();
//            foreach(QString code, i.key().split("|"))
//                message.replace(code, "<img src=\"" + i.value() + "\" />");
//	}
//    }

    // if the message is on same date show only time
    QString timeFormat;
//    if (timestamp.daysTo(QDateTime::currentDateTime()) == 0) {
//        timeFormat = "hh:mm:ss";
//    } else {
        timeFormat = "dd.MM.yyyy hh:mm:ss";
//    }

    QString formatMsg = style.replace("%name%", name)
                             .replace("%timestamp%", timestamp.toString(timeFormat))
                             .replace("%message%", msg);

    return formatMsg;
}

static bool getStyleInfo(QString stylePath, QString stylePathRelative, ChatStyleInfo &info)
{
    // Initialize info
    info = ChatStyleInfo();

    QFileInfo file(stylePath, "info.xml");

    QFile xmlFile(file.filePath());
    if (xmlFile.open(QIODevice::ReadOnly) == false) {
        // No info file found
        return false;
    }

    QDir dir(QApplication::applicationDirPath());

    QXmlStreamReader reader;
    reader.setDevice(&xmlFile);

    while (reader.atEnd() == false) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == "RetroShare_Style") {
                if (reader.attributes().value("version") == "1.0") {
                    info.stylePath = stylePathRelative;
                    continue;
                }
                // Not the right format of the xml file;
                return false ;
            }

            if (info.stylePath.isEmpty()) {
                continue;
            }

            if (reader.name() == "style") {
                // read style information
                while (reader.atEnd() == false) {
                    reader.readNext();
                    if (reader.isEndElement()) {
                        if (reader.name() == "style") {
                            break;
                        }
                        continue;
                    }
                    if (reader.isStartElement()) {
                        if (reader.name() == "name") {
                            info.styleName = reader.readElementText();
                            continue;
                        }
                        if (reader.name() == "description") {
                            info.styleDescription = reader.readElementText();
                            continue;
                        }
                        // ingore all other entries
                    }
                }
                continue;
            }

            if (reader.name() == "author") {
                // read author information
                while (reader.atEnd() == false) {
                    reader.readNext();
                    if (reader.isEndElement()) {
                        if (reader.name() == "author") {
                            break;
                        }
                        continue;
                    }
                    if (reader.isStartElement()) {
                        if (reader.name() == "name") {
                            info.authorName = reader.readElementText();
                            continue;
                        }
                        if (reader.name() == "email") {
                            info.authorEmail = reader.readElementText();
                            continue;
                        }
                        // ingore all other entries
                    }
                }
                continue;
            }
            // ingore all other entries
        }
    }

    if (reader.hasError()) {
        return false;
    }

    if (info.stylePath.isEmpty()) {
        return false;
    }
    return true;
}

static QString getBaseDir()
{
    // application path
    std::string configDir = RsInit::RsConfigDirectory();
    QString baseDir = QString::fromStdString(configDir);

#ifdef WIN32
    if (RsInit::isPortable ()) {
        // application dir for portable version
        baseDir = QApplication::applicationDirPath();
    }
#endif

    return baseDir;
}

/*static*/ bool ChatStyle::getAvailableStyles(enumStyleType styleType, QList<ChatStyleInfo> &styles)
{
    styles.clear();

    // base dir
    QDir baseDir(getBaseDir());

    ChatStyleInfo standardInfo;
    QString stylePath;

    switch (styleType) {
    case TYPE_PUBLIC:
        if (getStyleInfo(":/qss/chat/public", ":/qss/chat/public", standardInfo)) {
            standardInfo.styleDescription = tr("Standard style for public chat");
            styles.append(standardInfo);
        }
        stylePath = "stylesheets/public";
        break;
    case TYPE_PRIVATE:
        if (getStyleInfo(":/qss/chat/private", ":/qss/chat/private", standardInfo)) {
            standardInfo.styleDescription = tr("Standard style for private chat");
            styles.append(standardInfo);
        }
        stylePath = "stylesheets/private";
        break;
    case TYPE_HISTORY:
        if (getStyleInfo(":/qss/chat/history", ":/qss/chat/history", standardInfo)) {
            standardInfo.styleDescription = tr("Standard style for history");
            styles.append(standardInfo);
        }
        stylePath = "stylesheets/history";
        break;
    case TYPE_UNKNOWN:
    default:
        return false;
    }

    QDir dir(baseDir);
    if (dir.cd(stylePath) == false) {
        // no user styles available
        return true;
    }

    // get all style directories
    QFileInfoList dirList = dir.entryInfoList(QDir::AllDirs | QDir::NoDotAndDotDot, QDir::Name);

    // iterate style directories and get info
    for (QFileInfoList::iterator dir = dirList.begin(); dir != dirList.end(); dir++) {
        ChatStyleInfo info;
        if (getStyleInfo(dir->absoluteFilePath(), baseDir.relativeFilePath(dir->absoluteFilePath()), info)) {
            styles.append(info);
        }
    }

    return true;
}

/*static*/ bool ChatStyle::getAvailableVariants(const QString &stylePath, QStringList &variants)
{
    variants.clear();

    if (stylePath.isEmpty()) {
        return false;
    }

    // application path
    QDir dir(QApplication::applicationDirPath());
    if (dir.cd(stylePath) == false) {
        // style not found
        return false;
    }

    if (dir.cd("variants") == false) {
        // no variants available
        return true;
    }

    // get all variants
    QStringList filters;
    filters.append("*.css");
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files, QDir::Name);

    // iterate variants
    for (QFileInfoList::iterator file = fileList.begin(); file != fileList.end(); file++) {
        variants.append(file->baseName());
    }

    return true;
}
