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

#include "EventBroadcasterEditor.h"
#include "EventBroadcaster.h"


EventBroadcasterEditor::EventBroadcasterEditor(GenericProcessor* parentNode)
    : GenericEditor(parentNode)

{
    desiredWidth = 180;

    EventBroadcaster* p = (EventBroadcaster*)getProcessor();

    restartConnection = new UtilityButton("Restart Connection",Font("Default", 15, Font::plain));
    restartConnection->setBounds(20,32,150,22);
    restartConnection->addListener(this);
    addAndMakeVisible(restartConnection);

    urlLabel = new Label("Port", "Port:");
    urlLabel->setBounds(20, 66, 140, 20);
    addAndMakeVisible(urlLabel);

    portLabel = new Label("Port", String(p->getListeningPort()));
    portLabel->setBounds(70,66,80,20);
    portLabel->setFont(Font("Default", 15, Font::plain));
    portLabel->setColour(Label::textColourId, Colours::white);
    portLabel->setColour(Label::backgroundColourId, Colours::grey);
    portLabel->setEditable(true);
    portLabel->addListener(this);
    addAndMakeVisible(portLabel);

    formatLabel = new Label("Format", "Format:");
    formatLabel->setBounds(7, 97, 60, 25);
    addAndMakeVisible(formatLabel);

    formatBox = new ComboBox("FormatBox");
    formatBox->setBounds(67, 100, 100, 20);
    formatBox->addItem("JSON", EventBroadcaster::Format::JSON_STRING);
    formatBox->addItem("Raw Binary", EventBroadcaster::Format::RAW_BINARY);

    formatBox->setSelectedId(p->getOutputFormat());
    formatBox->addListener(this);
    addAndMakeVisible(formatBox);

}


void EventBroadcasterEditor::buttonClicked(Button* button)
{
    if (button == restartConnection)
    {
        EventBroadcaster* p = (EventBroadcaster*)getProcessor();
        int status = p->setListeningPort(p->getListeningPort(), true);

#ifdef ZEROMQ
        if (status != 0)
        {
            CoreServices::sendStatusMessage(String("Restart failed: ") + zmq_strerror(status));
        }
#endif
    }
}


void EventBroadcasterEditor::labelTextChanged(juce::Label* label)
{
    if (label == portLabel)
    {
        Value val = label->getTextValue();

        EventBroadcaster* p = (EventBroadcaster*)getProcessor();
        int status = p->setListeningPort(val.getValue());

#ifdef ZEROMQ
        if (status != 0)
        {
            CoreServices::sendStatusMessage(String("Port change failed: ") + zmq_strerror(status));
        }
#endif
    }
}


void EventBroadcasterEditor::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == formatBox)
    {
        auto p = static_cast<EventBroadcaster*>(getProcessor());
        p->setOutputFormat((EventBroadcaster::Format) comboBoxThatHasChanged->getSelectedId());
    }
}


void EventBroadcasterEditor::setDisplayedPort(int port)
{
    portLabel->setText(String(port), dontSendNotification);
}


void EventBroadcasterEditor::setDisplayedFormat(EventBroadcaster::Format format)
{
    formatBox->setSelectedId((int) format, dontSendNotification);
}