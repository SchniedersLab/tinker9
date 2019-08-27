#ifndef TINKER_ENERGY_BUFFER_H_
#define TINKER_ENERGY_BUFFER_H_

#include "gen_unit.h"
#include "mathfunc.h"
#include "rt.h"
#include <cassert>

TINKER_NAMESPACE_BEGIN
namespace ebuf_detail {
template <class T>
T fixed_to_floating_point(FixedPointType val) {
  assert(std::is_floating_point<T>::value);
  return static_cast<T>(static_cast<long long>(val)) / TINKER_FIXED_POINT;
}

template <class Answer>
struct EnergyBufferStorage {
  typedef Answer Type;
};

template <>
struct EnergyBufferStorage<float> {
  typedef FixedPointType Type;
};

// template <>
// struct EnergyBufferStorage<double> {
//   typedef FixedPointType Type;
// };

template <class Answer, class Store, int NAnswer, int NStore>
void reduce(Answer* __restrict__ host_ans, const Store* __restrict__ dptr,
            int nelem) {
  static_assert(NAnswer >= 1, "");
  static_assert(NStore >= NAnswer, "");

  Store val[NAnswer];
  if_constexpr(NAnswer == 1) { val[0] = reduce_sum(dptr, nelem); }
  else {
    reduce_sum2(val, NAnswer, dptr, nelem, NStore);
  }

  if_constexpr(std::is_same<Store, FixedPointType>::value &&
               !std::is_same<Store, Answer>::value) {
    if_constexpr(NAnswer == 1) {
      *host_ans = fixed_to_floating_point<Answer>(val[0]);
    }
    else {
      for (int i = 0; i < NAnswer; ++i)
        host_ans[i] = fixed_to_floating_point<Answer>(val[i]);
    }
  }
  else if_constexpr(std::is_same<Store, Answer>::value) {
    if_constexpr(NAnswer == 1) { *host_ans = val[0]; }
    else {
      for (int i = 0; i < NAnswer; ++i)
        host_ans[i] = val[i];
    }
  }
  else {
    assert(false);
  }
}
}

//====================================================================//

enum class GenericBufferVersion { A1S1, A9S16 };

//====================================================================//

template <class Answer, GenericBufferVersion VERSION>
struct GenericBufferReduce;

template <class Answer>
struct GenericBufferReduce<Answer, GenericBufferVersion::A1S1> {
  typedef typename ebuf_detail::EnergyBufferStorage<Answer>::Type Store;
  static constexpr int NAnswer = 1;
  static constexpr int NStore = 1;
  struct Reduce {
    void operator()(Answer* host_ans, const Store* dptr, int nelem) {
      ebuf_detail::reduce<Answer, Store, NAnswer, NStore>(host_ans, dptr,
                                                          nelem);
    }
  };
};

template <class Answer>
struct GenericBufferReduce<Answer, GenericBufferVersion::A9S16> {
  typedef typename ebuf_detail::EnergyBufferStorage<Answer>::Type Store;
  static constexpr int NAnswer = 9;
  static constexpr int NStore = 16;
  struct Reduce {
    void operator()(Answer* __restrict__ host_ans,
                    const Store* __restrict__ dptr, int nelem) {
      ebuf_detail::reduce<Answer, Store, NAnswer, NStore>(host_ans, dptr,
                                                          nelem);
    }
  };
};

//====================================================================//

template <class Answer, GenericBufferVersion VERSION>
class GenericBuffer {
private:
  typedef GenericBufferReduce<Answer, VERSION> R;
  typedef typename R::Store Store;

  static constexpr int NAnswer = R::NAnswer;
  static constexpr int NStore = R::NStore;
  static constexpr size_t rs = sizeof(Store) * NStore;

  Store* buf;
  int cap;

  void grow_if_must(int new_size) {
    if (new_size <= cap)
      return;

    int old_cap = cap;
    int new_cap = new_size;

    Store* new_buf;
    alloc_bytes(&new_buf, rs * new_cap);
    copy_bytes(new_buf, buf, rs * old_cap);
    dealloc_bytes(buf);

    buf = new_buf;
    cap = new_cap;
  }

public:
  typedef Store* PointerType;

  static int estimate_size(int nelem, int max_MB = 64) {
    size_t max_bytes = max_MB * 1024 * 1024ull;
    size_t n_elem_bytes = nelem * sizeof(Answer);
    size_t max_n_parallel = max_bytes / n_elem_bytes;

    constexpr size_t max_parallel = 2048;
    constexpr size_t min_parallel = 32;
    assert(max_n_parallel >= min_parallel && "must increase max_MB");

    size_t new_size = std::min(std::max(pow2_ge(nelem), min_parallel),
                               pow2_le(max_n_parallel));
    new_size = std::min(new_size, max_parallel);
    assert(is_pow2(new_size) && "new_size must be power of 2");

    return new_size;
  }

  void reduce(Answer* host_ans) {
    typename R::Reduce exec;
    exec(host_ans, buf, cap);
  }

  int size() const { return cap; }

  const Store* buffer() const { return buf; }

  Store* buffer() { return buf; }

  void alloc(int nelem, int max_MB = 64) {
    int new_size = estimate_size(nelem, max_MB);
    grow_if_must(new_size);
  }

  void zero() { zero_bytes(buf, rs * cap); }

  GenericBuffer()
      : buf(nullptr)
      , cap(0) {}

  ~GenericBuffer() {
    cap = 0;
    dealloc_bytes(buf);
    buf = nullptr;
  }
};

//====================================================================//

typedef GenericBuffer<int, GenericBufferVersion::A1S1> CountBuffer;
typedef GenericBuffer<real, GenericBufferVersion::A1S1> EnergyBuffer;
typedef GenericBuffer<real, GenericBufferVersion::A9S16> VirialBuffer;

typedef GenericUnit<CountBuffer, GenericUnitVersion::DisableOnDevice> Count;
typedef GenericUnit<EnergyBuffer, GenericUnitVersion::DisableOnDevice> Energy;
typedef GenericUnit<VirialBuffer, GenericUnitVersion::DisableOnDevice> Virial;

int get_count(Count);
double get_energy(Energy);
void get_virial(double*, Virial);

class BondedEnergy {
protected:
  int bufsize_;
  Virial vir_;
  Energy e_;

public:
  int buffer_size() const;
  Energy e();
  Virial vir();

  void dealloc();
  void alloc(int bsize);
};

class NonbondedEnergy : public BondedEnergy {
protected:
  Count ne_;

public:
  Count ne();

  void dealloc();
  void alloc(int bsize);
};
TINKER_NAMESPACE_END

#endif
