#include "compressor.hpp"

#include "arithmetic_decoder.hpp"
#include "arithmetic_encoder.hpp"
#include "compressor_state.hpp"
#include "data_bits_reader.hpp"
#include "data_compressed.hpp"
#include "data_compressed_reader.hpp"
#include "model.hpp"

//#include <iostream>
#include <sstream>

/// Define to print extra debug info during (de)compression.
#undef EXTRA_COMPRESSION_DEBUG

/// Define to test with trivial compression (no model).
#undef FCMP_COMPRESSION_TRIVIAL

using namespace fcmp;

/// Find greatest common divisor between two positive integers.
///
/// Uses Euclid's method.
///
/// \param lhs Left-hand-side operand.
/// \param rhs Right-hand-side operand.
/// \return Greatest common divisor or 1.
static uint8_t gcd(uint8_t lhs, uint8_t rhs)
{
  if(lhs < rhs)
  {
    std::swap(lhs, rhs);
  }

  for(;;)
  {
    if(1 >= rhs)
    {
      return rhs;
    }

    uint8_t rem = lhs % rhs;
    if(!rem)
    {
      return rhs;
    }

    lhs = rhs;
    rhs = rem;
  }
}

bool Compressor::addModel(uint8_t context, uint8_t weight)
{
  for(std::vector<Model>::iterator ii = m_models.begin(), ee = m_models.end(); (ii != ee); ++ii)
  {
    if(ii->matches(context))
    {
      if(0 >= weight)
      {       
        m_models.erase(ii);
        return true;
      }
      else if(ii->getWeight() != weight)
      {
        ii->setWeight(weight);
        return true;
      }
      return false;
    }
  }

  if(0 >= weight)
  {
    return false;
  }
  m_models.push_back(Model(context, weight));
  return true;
}

DataCompressedSptr Compressor::compressRun(const DataBits &data, size_t size_limit)
{
  DataCompressedSptr ret(new DataCompressed(data.getSizeBits(), *this));

  // If nothing to compress, exit immediately.
  if(data.empty())
  {
    return ret;
  }

  // Reset model counters and prepare reading.
  for(Model &vv : m_models)
  {
    vv.reset();
  }

  DataBitsReader reader(&data);
  ArithmeticEncoder coder;

  for(;;)
  {
    //std::cout << static_cast<double>(prob.getUpper() - prob.getLower()) /
    //  static_cast<double>(prob.getDenominator()) << std::endl;

    bool actual = reader.getCurrentBit();
    Probability prob = getProbability(reader, actual);

    size_t new_size = coder.encode(*ret, prob);
    if(new_size > size_limit)
    {
      // Abort if size is larger than comparable size.
      return ret;
    }

#if defined(FCMP_EXTRA_COMPRESSION_DEBUG)
  std::cout << "coded bit: " << bit << std::endl;
#endif

    update(reader, actual);

    if(!reader.advance())
    {
      break;
    }
  }

  coder.finishEncode(*ret);

  return ret;
}

Probability Compressor::getProbability(const DataBitsState &state, bool value) const
{
  unsigned count_one = 0;
  unsigned count_zero = 0;

  for(const Model& vv : m_models)
  {
    Prediction pre = vv.predict(state);

    if(pre.isValid())
    {
      count_one += pre.getCountOne() * vv.getWeight();
      count_zero += pre.getCountZero() * vv.getWeight();
    }
  }

  unsigned count_total = count_one + count_zero;
  if(1 == count_total)
  {
    std::ostringstream sstr;
    sstr << "illegal total value counts: " << count_one << " / " << count_zero;
    BOOST_THROW_EXCEPTION(std::runtime_error(sstr.str()));
  }
  else if(0 >= count_total)
  {
    count_one = 1;
    count_zero = 1;
    count_total = 2;
  }

#if defined(FCMP_COMPRESSION_TRIVIAL)
  return value ? Probability(1, 2, 2) : Probability(0, 1, 2);
#else
  if(value)
  {
    return Probability(count_zero, count_total, count_total);
  }
  return Probability(0, count_zero, count_total);
#endif
}

CompressorSptr Compressor::mutate(uint8_t context, uint8_t weight) const
{
  CompressorSptr ret(new Compressor(*this));

  if(!ret->addModel(context, weight))
  {
    return CompressorSptr();
  }

  return ret;
}

std::ostream& Compressor::put(std::ostream &ostr) const
{
  ostr << "[ ";

  {
    bool first = true;

    for(const Model &vv : m_models)
    {
      if(!first)
      {
        ostr << ", ";
      }
      first = false;

      ostr << vv;
    }
  }

  return ostr << " ]";
}

bool Compressor::rebase(bool rescale)
{
  unsigned DEFAULT_WEIGHT = 16;
  size_t ee = m_models.size();

  // Degenerate case.
  if(!ee)
  {
    return false;
  }

  uint8_t min_gcd = m_models[0].getWeight();
  uint8_t max_value = min_gcd;
  uint8_t min_value = min_gcd;

  // Find minimal common divisor.
  for(size_t ii = 1; (ii < ee); ++ii)
  {
    const Model &lhs = m_models[ii];

    for(size_t jj = 0; (jj < ii); ++jj)
    {
      const Model &rhs = m_models[jj];

      min_gcd = std::min(min_gcd, gcd(lhs.getWeight(), rhs.getWeight()));
    }

    max_value = std::max(max_value, lhs.getWeight());
    min_value = std::min(min_value, lhs.getWeight());
  }

  // Scale values down with minimal common divisor.
  max_value /= min_gcd;
  min_value /= min_gcd;

  uint8_t best_mul = 1;

  // Scale to fit squarely around default weight.
  if(rescale)
  {
    unsigned best_error = 0xFFFFFFFFU;

    for(unsigned ii = 1; (ii <= DEFAULT_WEIGHT); ++ii)
    {
      unsigned min_mul = static_cast<unsigned>(min_value) * ii;
      unsigned max_mul = static_cast<unsigned>(max_value) * ii;

      if(255 < max_mul)
      {
        break;
      }

      if((DEFAULT_WEIGHT >= min_mul) && (DEFAULT_WEIGHT <= max_mul))
      {
        unsigned error_up = DEFAULT_WEIGHT - min_mul;
        unsigned error_down = max_mul - DEFAULT_WEIGHT;
        unsigned error_sqr = (error_up * error_up) + (error_down * error_down);

        if(error_sqr < best_error)
        {
          best_error = error_sqr;
          best_mul = static_cast<uint8_t>(ii);
        }
      }
    }
  }

  // If the multiplier is not the divisor, there is something to do.
  if(best_mul != min_gcd)
  {
    for(Model &vv : m_models)
    {
      vv.setWeight(static_cast<uint8_t>((vv.getWeight() / min_gcd) * best_mul));
    }
    return true;
  }
  return false;
}

void Compressor::update(const DataBitsState &state, bool value)
{
  for(Model& vv : m_models)
  {
    vv.update(state, value);
  }
}

DataCompressedSptr Compressor::compress(const DataBits &data, unsigned threads)
{
  CompressorState cmp(&data, threads);

  while(cmp.compressCycle());

  return cmp.getBestData();
}

DataBitsUptr Compressor::extract(const DataCompressed &data)
{
  DataBitsUptr ret(new DataBits());

  // If nothing to extract, exit immediately.
  if(0 >= data.getExtractedSize())
  {
    return ret;
  }

  DataCompressedReader reader(&data);
  ArithmeticDecoder coder(reader);
  const size_t extracted_size = data.getExtractedSize();
  DataBitsState state;
  Compressor cmp;

  for(size_t ii = 0; (data.getModelCount() > ii); ++ii)
  {
    cmp.addModel(data.getContext(ii), data.getWeight(ii));
  }

  for(;;)
  {
    Probability prob = cmp.getProbability(state, true);

    bool decoded = coder.decode(*ret, reader, prob);

    // Exit if at end.
    if(ret->getSizeBits() >= extracted_size)
    {
      return ret;
    }

    cmp.update(state, decoded);
    state.advance(decoded);
  }
}

