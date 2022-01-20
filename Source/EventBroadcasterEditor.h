/*
  ==============================================================================

    EventBroadcasterEditor.h
    Created: 22 May 2015 3:34:30pm
    Author:  Christopher Stawarz

  ==============================================================================
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
