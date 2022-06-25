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

#include "EventBroadcaster.h"
#include "EventBroadcasterEditor.h"

#define SPIKE_BASE_SIZE 26
#define EVENT_BASE_SIZE 24

EventBroadcaster::ZMQContext::ZMQContext()
#ifdef ZEROMQ
    : context(zmq_ctx_new())
#else
    : context(nullptr)
#endif
{}

// ZMQContext is a ReferenceCountedObject with a pointer in each instance's
// socket pointer, so this only happens when the last instance is destroyed.
EventBroadcaster::ZMQContext::~ZMQContext()
{
#ifdef ZEROMQ
    zmq_ctx_destroy(context);
#endif
}

void* EventBroadcaster::ZMQContext::createZMQSocket()
{
#ifdef ZEROMQ
    jassert(context != nullptr);
    return zmq_socket(context, ZMQ_PUB);
#else
    jassertfalse; // should never be called in this case
    return nullptr;
#endif
}

EventBroadcaster::ZMQSocket::ZMQSocket()
    : socket    (nullptr)
    , boundPort (0)
{
#ifdef ZEROMQ
    socket = context->createZMQSocket();
#endif
}

EventBroadcaster::ZMQSocket::~ZMQSocket()
{
#ifdef ZEROMQ
    unbind(); // do this explicitly to free the port immediately
    zmq_close(socket);
#endif
}

bool EventBroadcaster::ZMQSocket::isValid() const
{
    return socket != nullptr;
}

int EventBroadcaster::ZMQSocket::getBoundPort() const
{
    return boundPort;
}

int EventBroadcaster::ZMQSocket::send(const void* buf, size_t len, int flags)
{
#ifdef ZEROMQ
    return zmq_send(socket, buf, len, flags);
#endif
    return 0;
}

int EventBroadcaster::ZMQSocket::bind(int port)
{
#ifdef ZEROMQ
    if (isValid() && port != 0)
    {
        int status = unbind();
        if (status == 0)
        {
            status = zmq_bind(socket, getEndpoint(port).toRawUTF8());
            if (status == 0)
            {
                boundPort = port;
            }
        }
        return status;
    }
#endif
    return 0;
}

int EventBroadcaster::ZMQSocket::unbind()
{
#ifdef ZEROMQ
    if (isValid() && boundPort != 0)
    {
        int status = zmq_unbind(socket, getEndpoint(boundPort).toRawUTF8());
        if (status == 0)
        {
            boundPort = 0;
        }
        return status;
    }
#endif
    return 0;
}


String EventBroadcaster::getEndpoint(int port)
{
    return String("tcp://*:") + String(port);
}


EventBroadcaster::EventBroadcaster()
    : GenericProcessor  ("Event Broadcaster")
    , listeningPort     (0)
    , outputFormat      (JSON_STRING)
{
    // set port to 5557; search for an available one if necessary; and do it asynchronously.
    setListeningPort(5557, false, true, false);
}


AudioProcessorEditor* EventBroadcaster::createEditor()
{
    editor = std::make_unique<EventBroadcasterEditor>(this);
    return editor.get();
}


int EventBroadcaster::getListeningPort() const
{
    if (zmqSocket == nullptr)
    {
        return 0;
    }
    return zmqSocket->getBoundPort();
}


int EventBroadcaster::setListeningPort(int port, bool forceRestart, bool searchForPort, bool synchronous)
{
    if (!synchronous)
    {
        const MessageManagerLock mmLock; // since the callback happens in the message thread

        asyncPort = port;
        asyncForceRestart = forceRestart;
        asyncSearchForPort = searchForPort;

        triggerAsyncUpdate();
        return 0;
    }

    int status = 0;
    //int currPort = getListeningPort();
    if ((listeningPort != port) || forceRestart)
    {
#ifdef ZEROMQ
        // unbind current socket (if any) to free up port
        if (zmqSocket != nullptr)
        {
            zmqSocket->unbind();
        }

        ScopedPointer<ZMQSocket> newSocket = new ZMQSocket();

        if (!newSocket->isValid())
        {
            status = zmq_errno();
            std::cout << "Failed to create socket: " << zmq_strerror(status) << std::endl;
        }
        else
        {
            if (searchForPort) // look for an unused port
            {
                while (0 != newSocket->bind(port) && zmq_errno() == EADDRINUSE)
                {
                    ++port;
                }
                if (newSocket->getBoundPort() != port)
                {
                    status = zmq_errno();
                }
            }
            else if (0 != newSocket->bind(port))
            {
                status = zmq_errno();
            }

            if (status != 0)
            {
                std::cout << "Failed to bind to port " << port << ": "
                    << zmq_strerror(status) << std::endl;
            }
            else
            {
                // success
                zmqSocket = newSocket;
                listeningPort = getListeningPort();
            }
        }


        if (status != 0 && zmqSocket != nullptr)
        {
            // try to rebind current socket to previous port
            zmqSocket->bind(listeningPort);
        }
#endif
    }

    // update editor
    auto editor = static_cast<EventBroadcasterEditor*>(getEditor());
    if (editor != nullptr)
    {
        editor->setDisplayedPort(getListeningPort());
    }
    return status;
}

EventBroadcaster::Format EventBroadcaster::getOutputFormat() const
{
    return outputFormat;
}


void EventBroadcaster::setOutputFormat(Format format)
{
    outputFormat = format;
}


void EventBroadcaster::process(AudioSampleBuffer& continuousBuffer)
{
    checkForEvents(true);
}

void EventBroadcaster::sendEvent(TTLEventPtr event) const
{
#ifdef ZEROMQ

    Array<MsgPart> message;

    uint16 baseType16 = 0; // 0 for TTL events, 1 for spikes
    message.add({ "type", { &baseType16, sizeof(baseType16) } });

    auto channel = event->getChannelInfo();

    if (outputFormat == RAW_BINARY) // serialize the event
    {
        size_t size = event->getChannelInfo()->getDataSize() 
            + event->getChannelInfo()->getTotalEventMetadataSize() + EVENT_BASE_SIZE;

        HeapBlock<char> buffer(size);

        event->serialize(buffer, size);

        message.add({ "data", { buffer, size } });
    }
    else // create a JSON string
    {
        DynamicObject::Ptr jsonObj = new DynamicObject();

        // Add common info to JSON
        jsonObj->setProperty("event_type", "ttl");
        jsonObj->setProperty("stream", channel->getStreamName());
        jsonObj->setProperty("source_node", channel->getNodeId());
        jsonObj->setProperty("sample_rate", channel->getSampleRate());
        jsonObj->setProperty("channel_name", channel->getName());
        jsonObj->setProperty("sample_number", event->getSampleNumber());
        jsonObj->setProperty("line", event->getLine());
        jsonObj->setProperty("state", event->getState());

        String jsonString = JSON::toString(var(jsonObj));
        message.add({ "json", { jsonString.toRawUTF8(), jsonString.getNumBytesAsUTF8() } });

    }

    sendMessage(message);

#endif

}

void EventBroadcaster::sendSpike(SpikePtr spike) const
{
#ifdef ZEROMQ

    Array<MsgPart> message;

    uint16 baseType16 = 1; // 0 for TTL events, 1 for spikes
    message.add({ "type", { &baseType16, sizeof(baseType16) } });

    auto channel = spike->getChannelInfo();

    if (outputFormat == RAW_BINARY) // serialize the spike
    {
        size_t size = SPIKE_BASE_SIZE
            + channel->getDataSize()
            + channel->getTotalEventMetadataSize()
            + channel->getNumChannels() * sizeof(float);

        HeapBlock<char> buffer(size);

        spike->serialize(buffer, size);

        message.add({ "data", { buffer, size } });
    }
    else // create a JSON string
    {
        DynamicObject::Ptr jsonObj = new DynamicObject();

        // Add common info to JSON
        jsonObj->setProperty("event_type", "spike");
        jsonObj->setProperty("stream", channel->getStreamName());
        jsonObj->setProperty("source_node", channel->getNodeId());
        jsonObj->setProperty("electrode", channel->getName());
        jsonObj->setProperty("num_channels", (int) channel->getNumChannels());
        jsonObj->setProperty("sample_rate", channel->getSampleRate());
        jsonObj->setProperty("sample_number", spike->getSampleNumber());
        jsonObj->setProperty("sorted_id", spike->getSortedId());

        // get channel amplitudes
        for (int ch = 0; ch < channel->getNumChannels(); ch++)
        {
            const float* data = spike->getDataPointer(ch);
            float amp = -data[channel->getPrePeakSamples() + 1];
            jsonObj->setProperty("amp" + String(ch + 1), amp);
        }
        
        String jsonString = JSON::toString(var(jsonObj));
        message.add({ "json", { jsonString.toRawUTF8(), jsonString.getNumBytesAsUTF8() } });

    }

    sendMessage(message);

#endif

}

int EventBroadcaster::sendMessage(const Array<MsgPart>& parts) const
{
#ifdef ZEROMQ
    int numParts = parts.size();
    for (int i = 0; i < numParts; ++i)
    {
        const MsgPart& part = parts.getUnchecked(i);
        int flags = (i < numParts - 1) ? ZMQ_SNDMORE : 0;
        if (-1 == zmqSocket->send(part.data.getData(), part.data.getSize(), flags))
        {
            std::cout << "Error sending " << part.name << ": " << zmq_strerror(zmq_errno()) << std::endl;
            return -1;
        }
    }
#endif
    return 0;
}

void EventBroadcaster::populateMetadata(const MetadataEventObject* channel,
    const EventBasePtr event, DynamicObject::Ptr dest)
{
    //Iterate through all event data and add to metadata object
    int numMetaData = event->getMetadataValueCount();
    for (int i = 0; i < numMetaData; i++)
    {
        //Get metadata name
        const MetadataDescriptor* metaDescPtr = channel->getEventMetadataDescriptor(i);
        const String& metaDataName = metaDescPtr->getName();

        //Get metadata value
        const MetadataValue* valuePtr = event->getMetadataValue(i);
        const void* rawPtr = valuePtr->getRawValuePointer();
        unsigned int length = valuePtr->getDataLength();

        auto dataReader = getDataReader(valuePtr->getDataType());
        dest->setProperty(metaDataName, dataReader(rawPtr, length));
    }
}


void EventBroadcaster::handleTTLEvent(TTLEventPtr event)
{
    sendEvent(event);
}

void EventBroadcaster::handleSpike(SpikePtr spike)
{
    sendSpike(spike);
}

void EventBroadcaster::saveCustomParametersToXml(XmlElement* parentElement)
{
    XmlElement* mainNode = parentElement->createNewChildElement("EVENTBROADCASTER");
    mainNode->setAttribute("port", listeningPort);
    mainNode->setAttribute("format", (int) outputFormat);
}


void EventBroadcaster::loadCustomParametersFromXml(XmlElement* parametersXml)
{

    forEachXmlChildElement(*parametersXml, mainNode)
    {
        if (mainNode->hasTagName("EVENTBROADCASTER"))
        {
            // overrides an existing async call to setListeningPort, if any
            setListeningPort(mainNode->getIntAttribute("port", listeningPort), false, false, false);

            outputFormat = (Format) mainNode->getIntAttribute("format", outputFormat);
            auto ed = static_cast<EventBroadcasterEditor*>(getEditor());
            if (ed)
            {
                ed->setDisplayedFormat(outputFormat);
            }
        }
    }
 
}

template <typename T>
var EventBroadcaster::binaryValueToVar(const void* value, unsigned int dataLength)
{
    auto typedValue = reinterpret_cast<const T*>(value);

    if (dataLength == 1)
    {
        return String(*typedValue);
    }
    else
    {
        Array<var> metaDataArray;
        for (unsigned int i = 0; i < dataLength; ++i)
        {
            metaDataArray.add(String(typedValue[i]));
        }
        return metaDataArray;
    }
}

var EventBroadcaster::stringValueToVar(const void* value, unsigned int dataLength)
{
    return String::createStringFromData(value, dataLength);
}

EventBroadcaster::DataToVarFcn EventBroadcaster::getDataReader(BaseType dataType)
{
    switch (dataType)
    {
    case BaseType::CHAR:
        return &stringValueToVar;

    case BaseType::INT8:
        return &binaryValueToVar<int8>;

    case BaseType::UINT8:
        return &binaryValueToVar<uint8>;

    case BaseType::INT16:
        return &binaryValueToVar<int16>;

    case BaseType::UINT16:
        return &binaryValueToVar<uint16>;

    case BaseType::INT32:
        return &binaryValueToVar<int32>;

    case BaseType::UINT32:
        return &binaryValueToVar<uint32>;

    case BaseType::INT64:
        return &binaryValueToVar<int64>;

    case BaseType::UINT64:
        return &binaryValueToVar<uint64>;

    case BaseType::FLOAT:
        return &binaryValueToVar<float>;

    case BaseType::DOUBLE:
        return &binaryValueToVar<double>;
    }
    jassertfalse;
    return nullptr;
}

void EventBroadcaster::handleAsyncUpdate()
{
    // should already be in the message thread, but just in case:
    const MessageManagerLock mmlock;

    setListeningPort(asyncPort, asyncForceRestart, asyncSearchForPort);
}
