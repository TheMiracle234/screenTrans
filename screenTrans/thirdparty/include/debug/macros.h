#ifndef TMUI_MACRO_H
#define TMUI_MACRO_H

#define TM_perror(x) std::cerr<<__FILE__<<" | line "<<__LINE__<<":\n"<<x<<"\n"
#define TM_sassert(x) static_assert(x)
#define TM_pause getchar()

// I love these stupied prints
#ifdef NDEBUG
#	define TM_assertOr(x, warn) if(x){;} // in O2, it will be optimized out, so no performance loss
#	define TM_println(x)
#	define TM_printv(x)
#else
#	define TM_assertOr(x, warn) if(!(x)) { TM_perror(warn); exit(1); }
#	define TM_println(x) std::cout<<x<<"\n"
#	define TM_printv(x) std::cout<<#x<<": "<<x<<"\n"
#endif

#define TM_public public
#define TM_private public
#define TM_protected protected

#endif // TMUI_MACRO_H