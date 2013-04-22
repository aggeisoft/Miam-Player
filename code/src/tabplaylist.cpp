#include "tabplaylist.h"
#include "library/libraryitem.h"
#include "settings.h"

#include <QAudio>
#include <QApplication>
#include <QFileSystemModel>
#include <QHeaderView>

#include "tabbar.h"
#include "treeview.h"

#include <QEventLoop>

/** Default constructor. */
TabPlaylist::TabPlaylist(QWidget *parent) :
	QTabWidget(parent), _tabIndex(-1)
{
	TabBar *tabBar = new TabBar(this);
	this->setTabBar(tabBar);
	this->setStyleSheet(Settings::getInstance()->styleSheet(this));
	this->setDocumentMode(true);
	messageBox = new TracksNotFoundMessageBox(this);
	_mediaPlayer = new QMediaPlayer(this);
	_mediaPlayer->setNotifyInterval(100);

	// Link core multimedia actions
	connect(_mediaPlayer, &QMediaPlayer::mediaStatusChanged, this, &TabPlaylist::mediaStatusChanged);

	// Keep playlists in memory before exit
	connect(qApp, &QCoreApplication::aboutToQuit, this, &TabPlaylist::savePlaylists);

	// Add a new playlist
	connect(this, &QTabWidget::currentChanged, this, &TabPlaylist::checkAddPlaylistButton);

	// Removing a playlist
	connect(this, &QTabWidget::tabCloseRequested, [=] (int index) {
		qDebug() << _mediaPlayer->state();
		if (_mediaPlayer->state() == QMediaPlayer::StoppedState) {
			this->removeTabFromCloseButton(index);
		} else {
			// QMediaPlayer slots are asynchronous, therefore it's necessary to keep functions arguments
			qDebug() << "lambda";
			_nextAction = "RemovePlaylist";
			_tabIndex = index;
			_mediaPlayer->stop();
		}
	});
	connect(_mediaPlayer, &QMediaPlayer::mediaChanged, [=](const QMediaContent & media) {
		qDebug() << "lambda mediaChanged!" << (media == NULL);
		if (media == NULL) {

		}
	});
	connect(_mediaPlayer, &QMediaPlayer::stateChanged, this, &TabPlaylist::dispatchState);
}

/** Retranslate tabs' name and all playlists in this widget. */
void TabPlaylist::retranslateUi()
{
	// No translation for the (+) tab button
	for (int i=0; i < count()-1; i++) {
		QString playlistName = tr("Playlist ");
		playlistName.append(QString::number(i+1));
		if (tabText(i) == playlistName) {
			setTabText(i, playlistName);
		}
		playlist(i)->retranslateUi();
	}
}

/** Change the current track. */
void TabPlaylist::skip(bool forward)
{
	if (_mediaPlayer->state() == QMediaPlayer::PlayingState) {
		// Is it strange? When it's playing, if signals aren't blocked a crash happens
		_mediaPlayer->blockSignals(true);
		_mediaPlayer->stop();
		forward ? currentPlayList()->mediaPlaylist()->next() : currentPlayList()->mediaPlaylist()->previous();
		_mediaPlayer->play();
		_mediaPlayer->blockSignals(false);
	} else {
		forward ? currentPlayList()->mediaPlaylist()->next() : currentPlayList()->mediaPlaylist()->previous();
		_mediaPlayer->play();
	}
}

/** Add external folders (from a drag and drop) to the current playlist. */
void TabPlaylist::addExtFolders(const QList<QDir> &folders)
{
	bool isEmpty = this->currentPlayList()->mediaPlaylist()->isEmpty();
	foreach (QDir folder, folders) {
		QDirIterator it(folder, QDirIterator::Subdirectories);
		QList<QMediaContent> medias;
		while (it.hasNext()) {
			medias.append(QMediaContent(QUrl::fromLocalFile(it.next())));
		}
		this->currentPlayList()->appendTracks(medias);
	}
	// Automatically plays the first track
	if (isEmpty) {
		this->skip();
	}
}

/** Add multiple tracks chosen by one from the library or the filesystem into a playlist. */
void TabPlaylist::addItemsToPlaylist(const QList<QPersistentModelIndex> &indexes)
{
	bool isEmpty = currentPlayList()->mediaPlaylist()->isEmpty();
	QList<QMediaContent> medias;
	foreach (QPersistentModelIndex index, indexes) {
		if (index.isValid()) {
			medias.append(QMediaContent(QUrl::fromLocalFile(TreeView::absFilePath(index))));
		}
	}
	currentPlayList()->appendTracks(medias);

	// Automatically plays the first track
	if (isEmpty && !medias.isEmpty()) {
		this->skip();
	}
}

/** Add a single track chosen by one from the library or the filesystem into the active playlist. */
void TabPlaylist::addItemToPlaylist(const QModelIndex &index)
{
	QList<QPersistentModelIndex> indexes;
	indexes.append(index);
	this->addItemsToPlaylist(indexes);
}

/** Add a new playlist tab. */
Playlist* TabPlaylist::addPlaylist()
{
	QString newPlaylistName = tr("Playlist ").append(QString::number(count()));

	// Then append a new empty playlist to the others
	Playlist *p = new Playlist(this, _mediaPlayer);
	p->init();
	int i = insertTab(count(), p, newPlaylistName);

	// If there's a custom stylesheet on the playlist, copy it from the previous one
	if (i > 1) {
		Playlist *previous = this->playlist(i - 1);
		p->setStyleSheet(previous->styleSheet());
		p->horizontalHeader()->setStyleSheet(previous->horizontalHeader()->styleSheet());
	}
	// Forward this signal to the MainWindow instance
	connect(p, &Playlist::selectedTracks, this, &TabPlaylist::aboutToChangeMenuLabels);

	// Select the new empty playlist
	setCurrentIndex(i);
	emit created();
	return p;
}

/** When the user is clicking on the (+) button to add a new playlist. */
void TabPlaylist::checkAddPlaylistButton(int i)
{
	// The (+) button is the last tab
	if (i == count()-1) {
		addPlaylist();
	} else {
		//currentPlayList()->countSelectedItems();
	}
}

/** Action sent from the menu. */
void TabPlaylist::removeCurrentPlaylist()
{
	removeTabFromCloseButton(this->tabBar()->currentIndex());
}

/** Remove a playlist when clicking on a close button in the corner. */
void TabPlaylist::removeTabFromCloseButton(int index)
{
	// Don't delete the first tab, if it's the last one remaining
	if (index > 0 || (index == 0 && count() > 2)) {
		// If the playlist we want to delete has no more right tab, then pick the left tab
		if (index + 1 > count() - 2) {
			setCurrentIndex(index - 1);
		}
		Playlist *p = playlist(index);
		this->removeTab(index);
		delete p;
		emit destroyed(index);
	} else {
		// Clear the content of first tab
		qDebug() << "clear the first tab" << _mediaPlayer->media().canonicalUrl() << _mediaPlayer->state() << _mediaPlayer->mediaStatus();

		//_mediaPlayer->setPlaylist(NULL); // crash
		//_mediaPlayer->setMedia(NULL); // crash
		//_mediaPlayer->setMedia(QMediaContent()); // crash

		currentPlayList()->mediaPlaylist()->clear(); // crash is mediaStatus != NoMedia
		currentPlayList()->model()->removeRows(0, currentPlayList()->model()->rowCount()); // ok
	}
}

/** Restore playlists at startup. */
void TabPlaylist::restorePlaylists()
{
	Settings *settings = Settings::getInstance();
	if (settings->playbackKeepPlaylists()) {
		QList<QVariant> playlists = settings->value("playlists").toList();
		if (!playlists.isEmpty()) {
			QList<QMediaContent> tracksNotFound;

			// For all playlists (stored as a pair of { QList<QVariant[Str=track]> ; QVariant[Str=playlist name] }
			for (int i = 0; i < playlists.size(); i++) {
				QList<QVariant> vTracks = playlists.at(i++).toList();
				Playlist *p = this->addPlaylist();
				if (i - 2 > 0) {
					this->setTabText(i - 2, playlists.at(i).toString());
				} else {
					this->setTabText(0, playlists.at(i).toString());
				}

				// For all tracks in one playlist
				QList<QMediaContent> medias;
				foreach (QVariant vTrack, vTracks) {
					QMediaContent track(vTrack.toUrl());
					if (!track.isNull()) {
						medias.append(track);
					} else {
						tracksNotFound.append(track);
					}
				}
				p->appendTracks(medias);
			}
			// Error handling
			if (!tracksNotFound.isEmpty()) {
				messageBox->displayError(TracksNotFoundMessageBox::RESTORE_AT_STARTUP, tracksNotFound);
			}
		} else {
			this->addPlaylist();
		}
	} else {
		this->addPlaylist();
	}
}

/** Seek backward in the current playing track for a small amount of time. */
void TabPlaylist::seekBackward()
{
	/*if (mediaObject->state() == PlayingState || mediaObject->state() == PausedState) {
		qint64 time = mediaObject->currentTime() - Settings::getInstance()->playbackSeekTime();
		if (time < 0) {
			mediaObject->seek(0);
		} else {
			mediaObject->seek(time);
		}
	}*/
	qDebug() << "TabPlaylist::seekBackward()";
}

/** Seek forward in the current playing track for a small amount of time. */
void TabPlaylist::seekForward()
{
	/*if (mediaObject->state() == PlayingState || mediaObject->state() == PausedState) {
		qint64 time = mediaObject->currentTime() + Settings::getInstance()->playbackSeekTime();
		if (time > mediaObject->totalTime()) {
			skipForward();
		} else {
			mediaObject->seek(time);
		}
	}*/
	//_mediaPlayer->seekableChanged();
	qDebug() << "TabPlaylist::seekForward()";
}

void TabPlaylist::dispatchState(QMediaPlayer::State newState)
{
	qDebug() << "TabPlaylist::dispatchState";
	if (newState == QMediaPlayer::StoppedState) {
		if (_nextAction == "RemovePlaylist" && _tabIndex >= 0) {
			qDebug() << "RemovePlaylist:" << _tabIndex;
			this->removeTabFromCloseButton(_tabIndex);
		} else {
			//qDebug() << "NOT RemovePlaylist";
			//emit stateChanged(newState);
		}
		_nextAction.clear();
		_tabIndex = -1;
	}
}

/** Save playlists before exit. */
void TabPlaylist::savePlaylists()
{
	Settings *settings = Settings::getInstance();
	if (settings->playbackKeepPlaylists()) {
		QList<QVariant> vPlaylists;
		// Iterate on all playlists, except empty ones and the last one (the (+) button)
		for (int i = 0; i < count() - 1; i++) {
			Playlist *p = playlist(i);

			if (!p->mediaPlaylist()->isEmpty()) {
				QList<QVariant> vTracks;
				for (int j = 0; j < p->mediaPlaylist()->mediaCount(); j++) {
					vTracks.append(p->mediaPlaylist()->media(j).canonicalUrl());
				}
				vPlaylists.append(QVariant(vTracks));
				vPlaylists.append(QVariant(tabBar()->tabText(i)));
			}
		}
		// Tracks are stored in QList< QList<QVariant> >
		settings->setValue("playlists", vPlaylists);
		settings->sync();
	}
}

void TabPlaylist::mediaStatusChanged(QMediaPlayer::MediaStatus newMediaState)
{
	qDebug() << "TabPlaylist::mediaStatusChanged" << newMediaState;
	if (newMediaState == QMediaPlayer::BufferedMedia) {
		this->currentPlayList()->highlightCurrentTrack();
	} else if (newMediaState == QMediaPlayer::EndOfMedia) {
		this->skip();
	}
}
