#ifndef SEEKBAR_H
#define SEEKBAR_H

#include <QSlider>

#include "mediaplayer.h"

/**
 * \brief       The SeekBar class is used to display a nice seek bar instead of default slider.
 * \author		Matthieu Bachelier
 * \copyright   GNU General Public License v3
 */
class SeekBar : public QSlider
{
    Q_OBJECT
private:
	/** Reference to the unique instance of MediaPlayer class in the application. */
	QWeakPointer<MediaPlayer> _mediaPlayer;

public:
    explicit SeekBar(QWidget *parent = 0);

	void setMediaPlayer(QWeakPointer<MediaPlayer> mediaPlayer);

protected:
	virtual void mouseMoveEvent(QMouseEvent *);

	virtual void mousePressEvent(QMouseEvent *);

	virtual void mouseReleaseEvent(QMouseEvent *);

	virtual void paintEvent(QPaintEvent *);

private:
	QLinearGradient interpolatedLinearGradient(const QPointF &start, const QPointF &end, QStyleOptionSlider &o);
};

#endif // SEEKBAR_H
