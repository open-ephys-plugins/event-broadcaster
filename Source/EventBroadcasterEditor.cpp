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

    restartConnection = new UtilityButton("Restart Connection");
    restartConnection->setBounds(20,34,150,22);
    restartConnection->addListener(this);
    addAndMakeVisible(restartConnection);

    addTextBoxParameterEditor(Parameter::PROCESSOR_SCOPE, "data_port", 20, 68);
    addComboBoxParameterEditor(Parameter::PROCESSOR_SCOPE, "output_format", 20, 99);
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