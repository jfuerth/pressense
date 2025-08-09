#ifndef STREAM_PROCESSOR_H
#define STREAM_PROCESSOR_H

namespace midi {

  class StreamProcessor {
  public:
    virtual ~StreamProcessor() = default;
    virtual void process(const uint8_t data) = 0;
  };

} // namespace midi

#endif // STREAM_PROCESSOR_H