#include <catch2/catch_test_macros.hpp>

#include "XP3Archive.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <zlib.h>

namespace {

    using namespace std::chrono_literals;

    template <typename T>
    class AsyncResult {
    public:
        void Complete(T value, std::exception_ptr error) {
            {
                std::lock_guard<std::mutex> lock(Mutex);
                Value = value;
                Error = error;
                ++Calls;
            }
            Ready.notify_all();
        }

        bool Wait() {
            std::unique_lock<std::mutex> lock(Mutex);
            return Ready.wait_for(lock, 5s, [this] { return Calls != 0; });
        }

        int CallCount() {
            std::lock_guard<std::mutex> lock(Mutex);
            return Calls;
        }

        T Value{};
        std::exception_ptr Error;
        int Calls = 0;

    private:
        std::mutex Mutex;
        std::condition_variable Ready;
    };

    class AsyncActionResult {
    public:
        void Complete(std::exception_ptr error) {
            {
                std::lock_guard<std::mutex> lock(Mutex);
                Error = error;
                ++Calls;
            }
            Ready.notify_all();
        }

        bool Wait() {
            std::unique_lock<std::mutex> lock(Mutex);
            return Ready.wait_for(lock, 5s, [this] { return Calls != 0; });
        }

        int CallCount() {
            std::lock_guard<std::mutex> lock(Mutex);
            return Calls;
        }

        std::exception_ptr Error;
        int Calls = 0;

    private:
        std::mutex Mutex;
        std::condition_variable Ready;
    };

    class MemoryBinaryStream : public tTJSBinaryStream {
    public:
        explicit MemoryBinaryStream(std::string data) : Data(std::move(data)) {}

        tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) override {
            tjs_int64 next = Position;
            if(whence == TJS_BS_SEEK_SET)
                next = offset;
            else if(whence == TJS_BS_SEEK_CUR)
                next += offset;
            else if(whence == TJS_BS_SEEK_END)
                next = static_cast<tjs_int64>(Data.size()) + offset;

            if(next >= 0 && next <= static_cast<tjs_int64>(Data.size()))
                Position = static_cast<std::size_t>(next);
            return Position;
        }

        tjs_uint Read(void *buffer, tjs_uint read_size) override {
            if(ThrowOnRead)
                throw std::runtime_error("test read failure");

            const auto available = Data.size() - Position;
            const auto count = std::min<std::size_t>(read_size, available);
            std::memcpy(buffer, Data.data() + Position, count);
            Position += count;
            return static_cast<tjs_uint>(count);
        }

        tjs_uint Write(const void *buffer, tjs_uint write_size) override {
            const auto required = Position + write_size;
            if(required > Data.size())
                Data.resize(required);
            std::memcpy(Data.data() + Position, buffer, write_size);
            Position += write_size;
            return write_size;
        }

        tjs_uint64 GetSize() override { return Data.size(); }

        bool ThrowOnRead = false;

    protected:
        std::string Data;
        std::size_t Position = 0;
    };

    class NativeAsyncMemoryStream final : public MemoryBinaryStream {
    public:
        explicit NativeAsyncMemoryStream(
            std::string data, std::atomic<bool> *destroyed = nullptr) :
            MemoryBinaryStream(std::move(data)), Destroyed(destroyed) {}

        ~NativeAsyncMemoryStream() override {
            if(Destroyed)
                *Destroyed = true;
        }

        tjs_uint Read(void *buffer, tjs_uint read_size) override {
            ++SynchronousReads;
            return MemoryBinaryStream::Read(buffer, read_size);
        }

        void ReadAsync(void *buffer, tjs_uint read_size,
                       tAsyncCallback<tjs_uint> completion) override {
            EnqueueAsyncOperation(
                [this, buffer, read_size, completion = std::move(completion)](
                    std::function<void()> finished) mutable {
                    ++AsynchronousReads;
                    try {
                        const auto available = Data.size() - Position;
                        const auto count =
                            std::min<std::size_t>(read_size, available);
                        std::memcpy(buffer, Data.data() + Position, count);
                        Position += count;
                        if(completion)
                            completion(static_cast<tjs_uint>(count), nullptr);
                    } catch(...) {
                        try {
                            if(completion)
                                completion(0, std::current_exception());
                        } catch(...) {
                        }
                    }
                    finished();
                });
        }

        std::atomic<int> SynchronousReads = 0;
        std::atomic<int> AsynchronousReads = 0;

    private:
        std::atomic<bool> *Destroyed;
    };

    class DestructionObservedStream final : public MemoryBinaryStream {
    public:
        DestructionObservedStream(std::string data,
                                  std::atomic<bool> &destroyed) :
            MemoryBinaryStream(std::move(data)), Destroyed(destroyed) {}

        ~DestructionObservedStream() override { Destroyed = true; }

    private:
        std::atomic<bool> &Destroyed;
    };

    std::string ExceptionMessage(const std::exception_ptr &error) {
        try {
            std::rethrow_exception(error);
        } catch(const std::exception &e) {
            return e.what();
        }
        return {};
    }

} // namespace

TEST_CASE("tTJSBinaryStream async callbacks are non-inline and exactly once") {
    MemoryBinaryStream stream("abc");
    std::array<char, 3> buffer{};
    AsyncResult<tjs_uint> result;
    AsyncResult<tjs_uint64> fence;
    const auto caller = std::this_thread::get_id();
    std::thread::id callback_thread;

    stream.ReadAsync(buffer.data(), buffer.size(),
                     [&](tjs_uint value, std::exception_ptr error) {
                         callback_thread = std::this_thread::get_id();
                         result.Complete(value, error);
                     });
    stream.GetSizeAsync([&](tjs_uint64 value, std::exception_ptr error) {
        fence.Complete(value, error);
    });

    REQUIRE(result.Wait());
    REQUIRE(fence.Wait());
    CHECK(result.CallCount() == 1);
    CHECK(result.Error == nullptr);
    CHECK(result.Value == 3);
    CHECK(callback_thread != caller);
    CHECK(std::string(buffer.data(), buffer.size()) == "abc");
}

TEST_CASE(
    "tTJSBinaryStream async cursor operations preserve submission order") {
    MemoryBinaryStream stream("abcdef");
    std::array<char, 2> first{};
    std::array<char, 2> second{};
    std::mutex order_mutex;
    std::condition_variable order_ready;
    std::vector<int> order;
    std::exception_ptr error;

    auto complete = [&](int sequence, std::exception_ptr current_error) {
        {
            std::lock_guard<std::mutex> lock(order_mutex);
            order.push_back(sequence);
            if(current_error)
                error = current_error;
        }
        order_ready.notify_all();
    };

    stream.SeekAsync(2, TJS_BS_SEEK_SET,
                     [&](tjs_uint64, std::exception_ptr e) { complete(1, e); });
    stream.ReadAsync(first.data(), first.size(),
                     [&](tjs_uint, std::exception_ptr e) { complete(2, e); });
    stream.SeekAsync(-1, TJS_BS_SEEK_CUR,
                     [&](tjs_uint64, std::exception_ptr e) { complete(3, e); });
    stream.ReadAsync(second.data(), second.size(),
                     [&](tjs_uint, std::exception_ptr e) { complete(4, e); });

    {
        std::unique_lock<std::mutex> lock(order_mutex);
        REQUIRE(
            order_ready.wait_for(lock, 5s, [&] { return order.size() == 4; }));
    }
    CHECK(error == nullptr);
    CHECK(order == std::vector<int>{ 1, 2, 3, 4 });
    CHECK(std::string(first.data(), first.size()) == "cd");
    CHECK(std::string(second.data(), second.size()) == "de");
}

TEST_CASE(
    "tTJSBinaryStream async helpers preserve short-read and error semantics") {
    SECTION("ReadAsync reports an EOF short read as success") {
        MemoryBinaryStream stream("xy");
        std::array<char, 4> buffer{};
        AsyncResult<tjs_uint> result;

        stream.ReadAsync(buffer.data(), buffer.size(),
                         [&](tjs_uint value, std::exception_ptr error) {
                             result.Complete(value, error);
                         });

        REQUIRE(result.Wait());
        CHECK(result.Error == nullptr);
        CHECK(result.Value == 2);
        CHECK(std::string(buffer.data(), 2) == "xy");
    }

    SECTION("ReadBufferAsync converts a short read to an error") {
        MemoryBinaryStream stream("xy");
        std::array<char, 4> buffer{};
        AsyncActionResult result;

        stream.ReadBufferAsync(
            buffer.data(), buffer.size(),
            [&](std::exception_ptr error) { result.Complete(error); });

        REQUIRE(result.Wait());
        REQUIRE(result.Error != nullptr);
        CHECK(result.CallCount() == 1);
    }

    SECTION("a synchronous primitive exception reaches the callback") {
        MemoryBinaryStream stream("xy");
        stream.ThrowOnRead = true;
        char byte = 0;
        AsyncResult<tjs_uint> result;

        stream.ReadAsync(&byte, 1,
                         [&](tjs_uint value, std::exception_ptr error) {
                             result.Complete(value, error);
                         });

        REQUIRE(result.Wait());
        REQUIRE(result.Error != nullptr);
        CHECK(result.Value == 0);
        CHECK(ExceptionMessage(result.Error) == "test read failure");
        CHECK(result.CallCount() == 1);
    }
}

TEST_CASE("tTJSBinaryStream async convenience operations compose primitives") {
    SECTION("position size and little-endian reads") {
        MemoryBinaryStream stream(
            std::string{ '\x01', '\x02', '\x03', '\x04' });
        AsyncActionResult positioned;
        AsyncResult<tjs_uint16> value;
        AsyncResult<tjs_uint64> position;
        AsyncResult<tjs_uint64> size;

        stream.SetPositionAsync(
            1, [&](std::exception_ptr error) { positioned.Complete(error); });
        stream.ReadI16LEAsync([&](tjs_uint16 result, std::exception_ptr error) {
            value.Complete(result, error);
        });
        stream.GetPositionAsync(
            [&](tjs_uint64 result, std::exception_ptr error) {
                position.Complete(result, error);
            });
        stream.GetSizeAsync([&](tjs_uint64 result, std::exception_ptr error) {
            size.Complete(result, error);
        });

        REQUIRE(positioned.Wait());
        REQUIRE(value.Wait());
        REQUIRE(position.Wait());
        REQUIRE(size.Wait());
        CHECK(positioned.Error == nullptr);
        CHECK(value.Error == nullptr);
        CHECK(value.Value == 0x0302);
        CHECK(position.Error == nullptr);
        CHECK(position.Value == 3);
        CHECK(size.Error == nullptr);
        CHECK(size.Value == 4);
    }

    SECTION("WriteBufferAsync and default SetEndOfStorageAsync semantics") {
        MemoryBinaryStream stream("ab");
        const std::array<char, 2> suffix{ 'c', 'd' };
        AsyncActionResult positioned;
        AsyncActionResult written;
        AsyncResult<tjs_uint64> size;
        AsyncActionResult set_end;

        stream.SetPositionAsync(
            2, [&](std::exception_ptr error) { positioned.Complete(error); });
        stream.WriteBufferAsync(
            suffix.data(), suffix.size(),
            [&](std::exception_ptr error) { written.Complete(error); });
        stream.GetSizeAsync([&](tjs_uint64 result, std::exception_ptr error) {
            size.Complete(result, error);
        });
        stream.SetEndOfStorageAsync(
            [&](std::exception_ptr error) { set_end.Complete(error); });

        REQUIRE(positioned.Wait());
        REQUIRE(written.Wait());
        REQUIRE(size.Wait());
        REQUIRE(set_end.Wait());
        CHECK(positioned.Error == nullptr);
        CHECK(written.Error == nullptr);
        CHECK(size.Error == nullptr);
        CHECK(size.Value == 4);
        CHECK(set_end.Error != nullptr);
    }
}

TEST_CASE("tTJSBinaryStream can be destroyed after its completion callback") {
    std::atomic<bool> destroyed = false;
    auto stream = std::make_unique<DestructionObservedStream>("z", destroyed);
    char byte = 0;
    AsyncResult<tjs_uint> result;

    stream->ReadAsync(&byte, 1, [&](tjs_uint value, std::exception_ptr error) {
        result.Complete(value, error);
    });

    REQUIRE(result.Wait());
    CHECK_FALSE(destroyed.load());
    stream.reset();
    CHECK(destroyed.load());
    CHECK(byte == 'z');
}

TEST_CASE("XP3 async operations preserve logical cursor across segments") {
    auto *owner = new tTVPXP3Archive(TJS_W("async-test.xp3"), 0);
    owner->ItemVector.resize(1);
    auto &item = owner->ItemVector[0];
    item.Name = TJS_W("entry.bin");
    item.OrgSize = 5;
    item.Segments = {
        { 0, 0, 2, 2, false },
        { 4, 2, 3, 3, false },
    };

    std::atomic<bool> storage_destroyed = false;
    auto *storage = new DestructionObservedStream("ab__cde", storage_destroyed);
    auto *stream = new tTVPXP3ArchiveStream(owner, 0, &item.Segments, storage,
                                            item.OrgSize);
    std::array<char, 4> buffer{};
    AsyncResult<tjs_uint64> seek;
    AsyncResult<tjs_uint> read;

    stream->SeekAsync(1, TJS_BS_SEEK_SET,
                      [&](tjs_uint64 value, std::exception_ptr error) {
                          seek.Complete(value, error);
                      });
    stream->ReadAsync(buffer.data(), buffer.size(),
                      [&](tjs_uint value, std::exception_ptr error) {
                          read.Complete(value, error);
                      });

    REQUIRE(seek.Wait());
    REQUIRE(read.Wait());
    CHECK(seek.Error == nullptr);
    CHECK(seek.Value == 1);
    CHECK(read.Error == nullptr);
    CHECK(read.Value == 4);
    CHECK(std::string(buffer.data(), buffer.size()) == "bcde");

    delete stream;
    if(!storage_destroyed.load())
        delete storage;
    owner->Release();
}

TEST_CASE("XP3 ReadAsync reports EOF short reads and underlying errors") {
    SECTION("logical EOF is a successful short read") {
        auto *owner = new tTVPXP3Archive(TJS_W("async-eof.xp3"), 0);
        owner->ItemVector.resize(1);
        auto &item = owner->ItemVector[0];
        item.Name = TJS_W("entry.bin");
        item.OrgSize = 2;
        item.Segments = { { 0, 0, 2, 2, false } };

        std::atomic<bool> storage_destroyed = false;
        auto *storage = new DestructionObservedStream("xy", storage_destroyed);
        auto *stream = new tTVPXP3ArchiveStream(owner, 0, &item.Segments,
                                                storage, item.OrgSize);
        std::array<char, 4> buffer{};
        AsyncResult<tjs_uint> result;

        stream->ReadAsync(buffer.data(), buffer.size(),
                          [&](tjs_uint value, std::exception_ptr error) {
                              result.Complete(value, error);
                          });

        REQUIRE(result.Wait());
        CHECK(result.Error == nullptr);
        CHECK(result.Value == 2);
        CHECK(std::string(buffer.data(), 2) == "xy");

        delete stream;
        if(!storage_destroyed.load())
            delete storage;
        owner->Release();
    }

    SECTION("an underlying short read is reported as an async error") {
        auto *owner = new tTVPXP3Archive(TJS_W("async-error.xp3"), 0);
        owner->ItemVector.resize(1);
        auto &item = owner->ItemVector[0];
        item.Name = TJS_W("entry.bin");
        item.OrgSize = 3;
        item.Segments = { { 0, 0, 3, 3, false } };

        std::atomic<bool> storage_destroyed = false;
        auto *storage = new DestructionObservedStream("x", storage_destroyed);
        auto *stream = new tTVPXP3ArchiveStream(owner, 0, &item.Segments,
                                                storage, item.OrgSize);
        std::array<char, 3> buffer{};
        AsyncResult<tjs_uint> result;

        stream->ReadAsync(buffer.data(), buffer.size(),
                          [&](tjs_uint value, std::exception_ptr error) {
                              result.Complete(value, error);
                          });

        REQUIRE(result.Wait());
        REQUIRE(result.Error != nullptr);
        CHECK(result.Value == 0);
        CHECK(result.CallCount() == 1);

        delete stream;
        if(!storage_destroyed.load())
            delete storage;
        owner->Release();
    }
}

TEST_CASE("XP3 continuation uses the underlying native async read path") {
    const std::string plain = "compressed continuation payload";
    std::vector<unsigned char> compressed(compressBound(plain.size()));
    uLongf compressed_size = compressed.size();
    REQUIRE(compress2(compressed.data(), &compressed_size,
                      reinterpret_cast<const Bytef *>(plain.data()),
                      plain.size(), Z_BEST_SPEED) == Z_OK);
    compressed.resize(compressed_size);

    auto *owner = new tTVPXP3Archive(TJS_W("async-compressed.xp3"), 0);
    owner->ItemVector.resize(1);
    auto &item = owner->ItemVector[0];
    item.Name = TJS_W("continuation.bin");
    item.OrgSize = plain.size();
    item.Segments = { { 0, 0, plain.size(), compressed.size(), true } };

    std::atomic<bool> storage_destroyed = false;
    auto *storage = new NativeAsyncMemoryStream(
        std::string(reinterpret_cast<const char *>(compressed.data()),
                    compressed.size()),
        &storage_destroyed);
    auto *stream = new tTVPXP3ArchiveStream(owner, 0, &item.Segments, storage,
                                            item.OrgSize);
    std::vector<char> output(plain.size());
    AsyncResult<tjs_uint> result;

    stream->ReadAsync(output.data(), output.size(),
                      [&](tjs_uint value, std::exception_ptr error) {
                          result.Complete(value, error);
                      });

    REQUIRE(result.Wait());
    CHECK(result.Error == nullptr);
    CHECK(result.Value == plain.size());
    CHECK(std::string(output.begin(), output.end()) == plain);
    CHECK(storage->SynchronousReads.load() == 0);
    CHECK(storage->AsynchronousReads.load() == 1);

    delete stream;
    if(!storage_destroyed.load())
        delete storage;
    owner->Release();
}
