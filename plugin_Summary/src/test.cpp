#include <AiLauncher.hpp>
#include <cassert>
#include <iostream>
#include <chrono>
#include <windows.h>
#include <semaphore>

using std::literals::operator""ms;

namespace util {
	class Timer {
	public:
		using Clock = std::chrono::steady_clock;
		using TimePoint = Clock::time_point;
		using Duration = std::chrono::duration<double>;   // 单位为秒的浮点时长

		/// 构造后立即开始计时
		Timer() : m_start(Clock::now()) {}

		/// 清零并重新开始
		void restart() { m_start = Clock::now(); }

		/// 已过时间（duration<double>，秒）
		Duration elapsed() const {
			return Clock::now() - m_start;
		}

		/// 以任意 chrono 单位返回，例如 elapsedAs<std::chrono::milliseconds>()
		template <typename Unit = std::chrono::seconds>
		double elapsedAs() const {
			return std::chrono::duration<double, typename Unit::period>(elapsed()).count();
		}

		double seconds()      const { return elapsed().count(); }
		double milliseconds() const { return elapsedAs<std::chrono::milliseconds>(); }
		double microseconds() const { return elapsedAs<std::chrono::microseconds>(); }
		double nanoseconds()  const { return elapsedAs<std::chrono::nanoseconds>(); }

	private:
		TimePoint m_start;
	};
}

int main() {

	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	minutes::AiLauncher ai;
	bool ok = ai.start("D:/visual studio/C++code/screenTrans/x64/Release/plugin/summary/default/models/qwen2.5-3b.gguf");
	if (!ok) {
		printf("not ok: %s\n", ai.lastError().c_str());
	}
	if (!ai.ready()) {
		printf("not ready: %s\n", ai.lastError().c_str());
	}

	std::binary_semaphore bs{0};

	std::thread t([&]() {
		util::Timer timer;
		timer.restart();
		auto result = ai.generate("总结以下内容",
			R"(产品：用户反馈会议录出来的视频音画不同步，差了大概 200ms。
			开发：我看了下，音频是按采集时间戳写的，视频是按渲染时间戳写的，两边时钟源不一样。
			测试：那怎么修？
			开发：我打算让视频也用音频的时间戳作为主时钟，音频追视频或者视频追音频都行。
			产品：多久能修好？
			开发：大概两天吧，还要回归测试一下其他功能。
			产品：那周五之前必须上线，不然下周用户会投诉。
			开发：行，我今晚加班先把补丁写出来，明天测试。)");
		std::cout << "result empty: " << (int)result.empty() << "\n";
		std::cout << "seconds: " << timer.seconds() << "\n";
	});

	std::this_thread::sleep_for(100ms);

	ai.cancel();
	//ai.stop();
	std::cout << "canceled: " << (int)ai.cancelled() << "\n";

	t.join();
}