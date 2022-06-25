/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2015 Christopher Stawarz and Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef EVENTBROADCASTEREDITOR_H_INCLUDED
#define EVENTBROADCASTEREDITOR_H_INCLUDED

#include <EditorHeaders.h>

#include "EventBroadcaster.h"

/**

 User interface for the "EventBroadcaster" sink.

 @see EventBroadcaster

 */

class EventBroadcasterEditor 
    : public GenericEditor
    , public Label::Listener
    , public ComboBox::Listener
    , public Button::Listener
{
public:

    /** Constructor*/
    EventBroadcasterEditor(GenericProcessor* parentNode);

    /** Destructor */
    ~EventBroadcasterEditor() { }

    /** Respond to button clicks*/
    void buttonClicked(Button* button) override;

    /** Respond to label edits */
    void labelTextChanged(Label* label) override;

    /** Respond to ComboBox changes*/
    void comboBoxChanged(ComboBox* comboBoxThatHasChanged) override;

    /** Sets the listening port*/
    void setDisplayedPort(int port);

    /** Sets the output format */
    void setDisplayedFormat(EventBroadcaster::Format format);

private:
    ScopedPointer<UtilityButton> restartConnection;
    ScopedPointer<Label> urlLabel;
    ScopedPointer<Label> portLabel;
    ScopedPointer<Label> formatLabel;
    ScopedPointer<ComboBox> formatBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EventBroadcasterEditor);

};


#endif  // EVENTBROADCASTEREDITOR_H_INCLUDED
