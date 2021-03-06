#pragma once
#include <vector>
#include <cstdint>
#include <list>
#include <opus.h>
#include "udp_client.h"
#include "snowflake.h"
#include "server.h"
#include "channel.h"
#include "message_receiver.h"
#include "timer.h"

namespace SleepyDiscord {
	class BaseDiscordClient;
	class VoiceConnection;

	class BaseVoiceEventHandler {
	public:
		//to do add more events
		virtual void onReady(VoiceConnection&) {}
	};

	struct VoiceContext {
		friend VoiceConnection;
		friend BaseDiscordClient;
	public:
		inline Snowflake<Channel> getChannelID() {
			return channelID;
		}
		inline Snowflake<Server> getServerID() {
			return serverID;
		}

		inline bool operator==(const VoiceContext& right) {
			return this == &right;
		}

		inline void setVoiceHandler(BaseVoiceEventHandler& handler) {
			eventHandler = &handler;
		}

	private:
		VoiceContext(Snowflake<Channel> _channelID, Snowflake<Server> _serverID) :
			channelID(_channelID), serverID(_serverID), eventHandler(nullptr)
		{}

		Snowflake<Channel> channelID;
		Snowflake<Server> serverID;
		std::string sessionID = "";
		std::string endpoint = "";
		std::string token; //the same token used to log in?
		BaseVoiceEventHandler* eventHandler;
	};

	enum AudioSourceType {
		AUDIO_BASE_TYPE,
		AUDIO_VECTOR,
		AUDIO_VECTOR_S16,
		AUDIO_POINTER,
		AUDIO_POINTER_S16
	};

	struct BaseAudioSource {
		BaseAudioSource() : type(AUDIO_BASE_TYPE) {}
		BaseAudioSource(AudioSourceType typ) : type(typ) {}
		virtual inline bool isOpusEncoded() { return false; }
		const AudioSourceType type;
		virtual ~BaseAudioSource() {}
	};

	//I'm not sure if it's really a good idea for this to be a nested class
	class VoiceConnection : public UDPClient, public GenericMessageReceiver {
	public:
		VoiceConnection(BaseDiscordClient* client, VoiceContext& _context) :
			origin(client), context(_context) {
		}

		VoiceConnection(VoiceConnection&&) = default;

		~VoiceConnection() {
		}

		inline const bool isReady() {
			return state & State::ABLE;
		}

		void disconnect();

		void startSpeaking(BaseAudioSource* source) {
			audioSource = std::unique_ptr<BaseAudioSource>(source);
			startSpeaking();
		}

		template<class AudioSource, class... Types>
		inline void startSpeaking(Types&&... arguments) {
			startSpeaking(new AudioSource(std::forward<Types>(arguments)...));
		}

	private:
		friend BaseDiscordClient;

		void processMessage(std::string message);

		enum VoiceOPCode {
			IDENTIFY            = 0,  //client begin a voice websocket connection
			SELECT_PROTOCOL     = 1,  //client select the voice protocol
			READY               = 2,  //server complete the websocket handshake
			HEARTBEAT           = 3,  //client keep the websocket connection alive
			SESSION_DESCRIPTION = 4,  //server describe the session
			SPEAKING            = 5,  //both   indicate which users are speaking
			HEARTBEAT_ACK       = 6,  //server sent immediately following a received client heartbeat
			RESUME              = 7,  //client resume a connection
			HELLO               = 8,  //server the continuous interval in milliseconds after which the client should send a heartbeat
			RESUMED             = 9,  //server acknowledge Resume
			CLIENT_DISCONNECT   = 13  //server a client has disconnected from the voice channel
		};
			
		enum State : uint8_t {
			NOT_CONNECTED = 0 << 0,
			CONNECTED     = 1 << 0,
			OPEN          = 1 << 1,
			AUDIO_ENABLED = 1 << 2,

			CAN_ENCODE    = 1 << 6,
			CAN_DECODE    = 1 << 7,

			ABLE          = CONNECTED | OPEN | AUDIO_ENABLED,
		};

		int heartbeatInterval = 0;
		uint32_t sSRC;
		uint16_t port;
		BaseDiscordClient* origin;
		Timer heart;
		State state = State::NOT_CONNECTED;
		int16_t numOfPacketsSent = 0;
		std::unique_ptr<BaseAudioSource> audioSource;
		std::size_t samplesSentLastTime = 0;
		VoiceContext& context;
#if !defined(NONEXISTENT_OPUS)
		OpusEncoder *encoder;
#endif
		uint16_t sequence = 0;
		uint32_t timestamp = 0;

		#define SECRET_KEY_SIZE 32
		unsigned char secretKey[SECRET_KEY_SIZE];

		void heartbeat();
		void startSpeaking();
		void sendSpeaking(bool isNowSpeaking);
		void speak();
		void speak(int16_t*& audioData, const std::size_t& length);
		void sendAudioData(
			uint8_t*& encodedAudioData,
			const std::size_t & length,
			const std::size_t & frameSize
		);
	};

	struct AudioTransmissionDetails {
	public:
		inline VoiceContext& context() {
			return _context;
		}

		inline std::size_t proposedLength() {
			return _proposedLength;
		}

		inline std::size_t amountSentSinceLastTime() {
			return _amountSentSinceLastTime;
		}
	private:
		friend VoiceConnection;
		AudioTransmissionDetails(VoiceContext& con, const std::size_t pro, const std::size_t amo) :
			_context(con), _proposedLength(pro), _amountSentSinceLastTime(amo) { }

		VoiceContext& _context;
		const std::size_t _proposedLength;
		const std::size_t _amountSentSinceLastTime;
	};

	template<AudioSourceType Type>
	struct AudioSource : public BaseAudioSource {
		const AudioSourceType type = Type;
	};

	template<>
	struct AudioSource<AUDIO_VECTOR> : public BaseAudioSource {
		AudioSource() : BaseAudioSource(AUDIO_VECTOR_S16) {}
		virtual std::vector<int16_t> read(AudioTransmissionDetails& details) {
			return std::vector<int16_t>();
		};
	};

	template<>
	struct AudioSource<AUDIO_POINTER> : public BaseAudioSource {
		AudioSource() : BaseAudioSource(AUDIO_POINTER_S16) {}
		virtual void read(AudioTransmissionDetails& details, int16_t*& buffer, std::size_t& length) {};
	};
}