#pragma once

#include <concepts>

namespace gl {

	template<typename T>
		requires std::equality_comparable<T> && std::copyable<T>
	class ChangeRecorder {
	private:
		T m_pre;
		const T* m_now;
	public:
		ChangeRecorder(const T& var) : m_now(&var), m_pre(var) {}
		template<bool updateInside>
		bool changed() {
			bool res = *m_now != m_pre;
			if constexpr (updateInside) {
				update();
			}
			return res;
		}
		void update() { m_pre = *m_now; }
	};

}