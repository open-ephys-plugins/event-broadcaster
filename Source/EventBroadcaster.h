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

#ifndef EVENTBROADCASTER_H_INCLUDED
#define EVENTBROADCASTER_H_INCLUDED

#include <ProcessorHeaders.h>

#ifdef ZEROMQ
    #ifdef WIN32
        #include <zmq.h>
        #include <zmq_utils.h>
    #else
        #include <zmq.h>
    #endif
#endif

class EventBroadcaster : public GenericProcessor
                       , private AsyncUpdater
{
public:
    /** ids for format combobox */
    enum Format { RAW_BINARY = 1, JSON_STRING = 2};

    /** Constructor */
    EventBroadcaster();

    /** Destructor */
    ~EventBroadcaster() { }

    /** Create custom editor*/
    AudioProcessorEditor* createEditor() override;

    /** Returns the current listening port number */
    int getListeningPort() const;
    
    /**Returns 0 on success, else the errno value for the error that occurred. */
    int setListeningPort(int port, bool forceRestart = false, bool searchForPort = false, bool synchronous = true);

    /** Returns the output format (RAW_BINARY, HEADER_ONLY, HEADER_AND_JSON) */
    Format getOutputFormat() const;

    /** Sets the output format*/
    void setOutputFormat(Format format);

    /** Streams events via ZMQ */
    void process(AudioBuffer<float>& continuousBuffer) override;

    /** Called whenever a new event is received */
    void handleTTLEvent(TTLEventPtr event) override;

    /** Called whenever a new spike is received */
    void handleSpike(SpikePtr spike) override;

    /** Saves parameters*/
    void saveCustomParametersToXml(XmlElement* parentElement) override;

    /** Loads parameters*/
    void loadCustomParametersFromXml(XmlElement* parameters) override;

private:
    struct MsgPart
    {
        String name;
        MemoryBlock data;
    };

    class ZMQContext : public ReferenceCountedObject
    {
    public:
        ZMQContext();
        ~ZMQContext();
        void* createZMQSocket();
    private:
        void* context;
    };

    class ZMQSocket
    {
    public:
        ZMQSocket();
        ~ZMQSocket();

        bool isValid() const;
        int getBoundPort() const;

        int send(const void* buf, size_t len, int flags);
        int bind(int port);
        int unbind();
    private:
        int boundPort;
        void* socket;

        // see here for why the context can't just be static:
        // https://github.com/zeromq/libzmq/issues/1708
        SharedResourcePointer<ZMQContext> context;
    };

    /** Sends an event over ZMQ */
    void sendEvent(TTLEventPtr event) const;

    /** Sends a spike over ZMQ */
    void sendSpike(SpikePtr spike) const;

    /** Sends a multi-part ZMQ message */
    int sendMessage(const Array<MsgPart>& parts) const;

    // add metadata from an event to a DynamicObject
    static void populateMetadata(const MetadataEventObject* channel,
                                 const EventBasePtr event, DynamicObject::Ptr dest);

    void handleAsyncUpdate() override; // to change port asynchronously

    static String getEndpoint(int port);

    // called from setListeningPort() depending on success/failure of ZMQ operations
    void reportActualListeningPort(int port);

    // share a "dumb" pointer that doesn't take part in reference counting.
    // want the context to be terminated by the time the static members are
    // destroyed (see: https://github.com/zeromq/libzmq/issues/1708)
    static ZMQContext* sharedContext;
    static CriticalSection sharedContextLock;
    ScopedPointer<ZMQSocket> zmqSocket;
    int listeningPort;

    Format outputFormat;

    // ---- utilities for formatting binary data and metadata ----

    // a fuction to convert metadata or binary data to a form we can add to the JSON object
    typedef var(*DataToVarFcn)(const void* value, unsigned int dataLength);

    template <typename T>
    static var binaryValueToVar(const void* value, unsigned int dataLength);

    static var stringValueToVar(const void* value, unsigned int dataLength);

    static DataToVarFcn getDataReader(BaseType dataType);

    // for setting port asynchronously
    int asyncPort;
    bool asyncForceRestart;
    bool asyncSearchForPort;
};


#endif  // EVENTBROADCASTER_H_INCLUDED
