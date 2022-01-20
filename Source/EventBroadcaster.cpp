/*
  ==============================================================================

    EventBroadcaster.cpp
    Created: 22 May 2015 3:31:50pm
    Author:  Christopher Stawarz

  ==============================================================================
*/

#include "EventBroadcaster.h"
#include "EventBroadcasterEditor.h"

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
    , outputFormat      (HEADER_AND_JSON)
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

void EventBroadcaster::sendEvent(const ChannelInfoObject* channel, const MidiMessage& msg) const
{
#ifdef ZEROMQ
    // TODO Create a procotol that has outline for every type of event
    int currFormat = outputFormat;
    Array<MsgPart> message;

    // common info that isn't type-specific
    Event::Type baseType = Event::getBaseType(msg);
    const String& identifier = channel->getIdentifier();
    float sampleRate = channel->getSampleRate();
    int64 timestamp = Event::getTimestamp(msg);

    if (currFormat == RAW_BINARY)
    {
        uint16 baseType16 = static_cast<uint16>(baseType); // for backward compatability
        double timestampSeconds = double(timestamp) / sampleRate;
        const void* rawData = msg.getRawData();
        size_t rawDataSize = msg.getRawDataSize();

        message.add({ "base type", { &baseType16, sizeof(baseType16) } });
        message.add({ "timestamp", { &timestampSeconds, sizeof(timestampSeconds) } });
        message.add({ "raw data", { rawData, rawDataSize } });
    }
    else // deserialize the data, get metadata, etc.
    {
        // info to be assigned depending on the event type
        String header;
        DynamicObject::Ptr jsonObj = new DynamicObject();
        EventBasePtr baseEvent;
        const MetadataEventObject* metaDataChannel;

        // deserialize event and get type-specific information
        switch (baseType)
        {
        case Event::SPIKE_EVENT:
        {
            auto spikeChannel = static_cast<const SpikeChannel*>(channel);
            metaDataChannel = static_cast<const MetadataEventObject*>(spikeChannel);

            baseEvent = Spike::deserialize(msg, spikeChannel).release();
            auto spike = static_cast<Spike*>(baseEvent.get());

            // create header
            uint16 sortedID = spike->getSortedID();
            header = "spike/sortedid:" + String(sortedID) + "/id:" + identifier + "/ts:" + String(timestamp);

            if (currFormat == HEADER_AND_JSON)
            {
                // add info to JSON
                jsonObj->setProperty("type", "spike");
                jsonObj->setProperty("sortedID", sortedID);

                int spikeChannels = spikeChannel->getNumChannels();
                jsonObj->setProperty("numChannels", spikeChannels);

                Array<var> thresholds;
                for (int i = 0; i < spikeChannels; ++i)
                {
                    thresholds.add(spike->getThreshold(i));
                }
                jsonObj->setProperty("threshold", thresholds);
            }

            break;  // case SPIKE_EVENT
        }

        case Event::PROCESSOR_EVENT:
        {
            auto eventChannel = static_cast<const EventChannel*>(channel);
            metaDataChannel = static_cast<const MetadataEventObject*>(eventChannel);

            baseEvent = Event::deserialize(msg, eventChannel).release();
            auto event = static_cast<Event*>(baseEvent.get());

            // for json
            var type;
            var data;

            auto eventType = event->getEventType();
            switch (eventType)
            {
            case EventChannel::Type::TTL:
            {
                bool state = static_cast<TTLEvent*>(event)->getState();

                uint8 channel = static_cast<TTLEvent*>(event)->getBit();

                header = "ttl/channel:" + String(channel) + "/state:" + (state ? "1" : "0") +
                    "/id:" + identifier + "/ts:" + String(timestamp);

                type = "ttl";
                data = state;
                break;
            }

            case EventChannel::Type::TEXT:
            {
                const String& text = static_cast<TextEvent*>(event)->getText();

                header = "text/id:" + identifier +
                    "/text:" + text + "/ts:" + String(timestamp);

                type = "text";
                data = text;
                break;
            }

            /*default:
            {
                if (eventType < EventChannel::Type::BINARY ||
                    eventType >= EventChannel::EventChannelTypes::INVALID)
                {
                    jassertfalse;
                    return;
                }

                // must have binary event

                BaseType dataType = eventChannel->getEquivalentMetadataType();
                auto dataReader = getDataReader(dataType);
                const void* rawData = static_cast<BinaryEvent*>(event)->getBinaryDataPointer();
                unsigned int length = eventChannel->getLength();

                type = "binary";
                data = dataReader(rawData, length);

                String dataString;
                if (data.isArray()) // make comma-separated list of values
                {
                    int length = data.size();
                    for (int i = 0; i < length; ++i)
                    {
                        if (i > 0) { dataString += ","; }
                        dataString += data[i].toString();
                    }
                }
                else
                {
                    dataString = data.toString();
                }

                header = "binary/channel:" + String(channel) + "/id:" + identifier +
                    "/data:" + dataString + "/ts:" + String(timestamp);

                break;
            }*/
            } // end switch(eventType)

            if (currFormat == HEADER_AND_JSON)
            {
                //jsonObj->setProperty("channel", channel);
                jsonObj->setProperty("type", type);
                jsonObj->setProperty("data", data);
            }

            break; // case PROCESSOR_EVENT
        }

        default:
            jassertfalse; // should never happen
            return;

        } // end switch(baseType)
        message.add({ "header", { header.toRawUTF8(), header.getNumBytesAsUTF8() } });

        String jsonString; // must be outside the if-statement so it remains in scope when we send the message
        if (currFormat == HEADER_AND_JSON)
        {
            // Add common info to JSON
            jsonObj->setProperty("timestamp", timestamp);
            jsonObj->setProperty("sample_rate", sampleRate);
            jsonObj->setProperty("identifier", identifier);
            jsonObj->setProperty("name", channel->getName());

            // Add metadata
            DynamicObject::Ptr metaDataObj = new DynamicObject();
            populateMetadata(metaDataChannel, baseEvent, metaDataObj);
            jsonObj->setProperty("metaData", metaDataObj.get());

            String jsonString = JSON::toString(var(jsonObj));
            message.add({ "json", { jsonString.toRawUTF8(), jsonString.getNumBytesAsUTF8() } });
        }
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


void EventBroadcaster::handleEvent(const EventChannel* channelInfo, const EventPacket& event, int samplePosition)
{
    sendEvent(channelInfo, event);
}

void EventBroadcaster::handleSpike(const SpikeChannel* channelInfo, const EventPacket& event, int samplePosition)
{
    sendEvent(channelInfo, event);
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
