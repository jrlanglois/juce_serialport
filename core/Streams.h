
class JUCE_API SerialPortConfig final
{
public:
    SerialPortConfig() = default;

    enum class StopBits
    {
        one,
        oneAndHalf,
        two
    };

    enum class FlowControl
    {
        none,
        hardware,
        xOnxOff
    };

    enum class Parity
    {
        none,
        odd,
        even,
        space,
        mark
    };

    SerialPortConfig (uint32_t bps, uint32_t databits,
                      Parity parity,
                      StopBits stopbits,
                      FlowControl flowcontrol) :
        bps (bps),
        databits (databits),
        parity (parity),
        stopbits (stopbits),
        flowcontrol (flowcontrol)
    {
    }

    uint32_t bps = 9600;
    uint32_t databits = 9600;
    Parity parity;
    StopBits stopbits = StopBits::one;
    FlowControl flowcontrol = FlowControl::none;
};

/** Serial port class for accessing serial ports in an asynchronous buffered manner

    contributed by graffiti
    
    Updated for current Juce API 8/1/12 Marc Lindahl

    a typical serialport scenario may look like this:
    @code
    {
        //get a list of serial ports installed on the system, as a StringPairArray containing a friendly name and the port path
        const auto portlist = SerialPort::getSerialPortPaths();
        if (! portlist.empty())
        {
            //open the first port on the system
            SerialPort serialPort (portlist.getAllValues()[0], SerialPortConfig (9600, 8, SerialPortConfig::none, SerialPortConfig::one, SerialPortConfig::none));

            if (! serialPort.exists())
                return;

            //create streams for reading/writing
            SerialPortOutputStream outputStream (serialPort);
            SerialPortInputStream inputStream (serialPort);

            //write some bytes
            pOutputStream->write ("hello world via serial", 22);

            //read chars one at a time:
            char c = 0;
            while (! inputStream.isExhausted())
                inputStream.read (&c, 1);

            //or, read line by line:
            String s;
            while (inputStream.canReadLine())
                s = inputStream.readNextLine();

            //or ask to be notified when a new line is available:
            inputStream.addChangeListener (this); //we must be a ChangeListener to receive notifications
            inputStream.setNotify (SerialPortInputStream::NOTIFY_ON_CHAR, '\n');

            //or ask to be notified whenever any character is received
            //NOTE - use with care at high baud rates!!!!
            inputStream.setNotify(SerialPortInputStream::NOTIFY_ALWAYS);

            //please see class definitions for other features/functions etc        
        }
    }
    @endcode
*/
class JUCE_API SerialPort
{
public:
    using DebugFunction = std::function<void (String, String)>;

    SerialPort (DebugFunction theDebugLog) : DebugLogInternal (theDebugLog)
    {
        portHandle = 0;
        portDescriptor = -1;
    }
    SerialPort (const String& portPath, DebugFunction theDebugLog) : SerialPort (theDebugLog)
    {
        open(portPath);
    }
    SerialPort (const String& portPath, const SerialPortConfig& config, DebugFunction theDebugLog) : SerialPort (theDebugLog)
    {
        open(portPath);
        setConfig(config);
    }
    virtual ~SerialPort()
    {
        close();
    }
    bool open(const String & portPath);
    void close();
    bool setConfig(const SerialPortConfig & config);
    bool getConfig(SerialPortConfig& config);
    const String& getPortPath() const noexcept { return portPath; }
    static StringPairArray getSerialPortPaths();
    bool exists() const;
    virtual void cancel ();
    void DebugLog (String prefix, String msg) { if (DebugLogInternal != nullptr) DebugLogInternal (prefix, msg); }

    juce_UseDebuggingNewOperator
private:
    friend class SerialPortInputStream;
    friend class SerialPortOutputStream;
    void* portHandle = nullptr;
    int portDescriptor;
    bool canceled;
    String portPath;

    DebugFunction DebugLogInternal;

#if JUCE_ANDROID
    jobject usbSerialHelper;
#endif
};

class JUCE_API SerialPortInputStream final : public InputStream,
                                             public ChangeBroadcaster,
                                             private Thread
{
public:
    SerialPortInputStream (SerialPort* port) :
        Thread ("SerialInThread"),
        port (port)
    {
        startThread();
    }

    ~SerialPortInputStream() override
    {
        signalThreadShouldExit();
        cancel ();
        waitForThreadToExit (5000);
    }

    enum class NotificationType
    {
        off,
        onChar,
        always
    };

    void setNotify (NotificationType type = NotificationType::onChar, char c = 0)
    {
        notify = type;
        notifyChar = c;
    }

    bool canReadString() const
    {
        const ScopedLock l (bufferCriticalSection);
        int i = 0;
        while (i < numBufferedBytes)
            if (buffer[i++] == 0)
                return true;

        return false;
    }

    bool canReadLine() const
    {
        const ScopedLock l (bufferCriticalSection);
        int i = 0;
        while (i < numBufferedBytes)
            if (buffer[i++] == '\n')
                return true;

        return false;
    }

    virtual void cancel ();
    const SerialPort* getPort() const { return port; }
    void setReaderPriority (int priority) { setPriority (priority); }

    /** Have to override this because InputStream::readNextLine isn't compatible with SerialPorts. */
    String readNextLine() override
    {
        String s;
        char c = 0;
        s.preallocateBytes (32);
        while (read (&c, 1) && (c != '\n'))
            s.append (String::charToString (c), 1);

        return s.trim();
    }

    int64 getTotalLength() override
    {
        const ScopedLock l (bufferCriticalSection);
        return numBufferedBytes;
    }

    bool isExhausted() override
    {
        const ScopedLock l (bufferCriticalSection);
        return numBufferedBytes != 0;
    }

    /** @internal */
    void run() override;
    /** @internal */
    int read (void*, int) override;
    /** @internal */
    int64 getPosition() override        { return 0; }
    /** @internal */
    bool setPosition (int64) override   { return false; }

private:
    SerialPort* port = nullptr;
    int64 numBufferedBytes = 0;
    MemoryBlock buffer;
    mutable CriticalSection bufferCriticalSection;
    NotificationType notify = NotificationType::off;
    char notifyChar = 0;

    bool canReadLine (StringRef s) const
    {
        const ScopedLock l (bufferCriticalSection);
        int i = 0;
        while (i < numBufferedBytes)
            if (String (buffer[i++]) == s)
                return true;

        return false;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerialPortInputStream)
};

class JUCE_API SerialPortOutputStream final : public OutputStream,
                                              private Thread
{
public:
    SerialPortOutputStream (SerialPort* port) :
        Thread ("SerialOutThread"),
        port (port),
        numBufferedBytes (0)
    {
        startThread();
    }

    ~SerialPortOutputStream() override
    {
        signalThreadShouldExit();
        cancel ();
        waitForThreadToExit (5000);
    }

    void run() override;
    virtual void flush() { }
    virtual bool setPosition(int64 /*newPosition*/){return false;}
    virtual int64 getPosition(){return -1;}
    virtual bool write(const void *dataToWrite, size_t howManyBytes);
    virtual void cancel ();
    SerialPort* getPort() { return port; }
    void setWriterPriority (int priority) { setPriority (priority); }

private:
    SerialPort* port = nullptr;
    int numBufferedBytes = 0;
    MemoryBlock buffer;
    CriticalSection bufferCriticalSection;
    WaitableEvent triggerWrite;
    static const uint32_t writeBufferSize = 128;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerialPortInputStream)
};
