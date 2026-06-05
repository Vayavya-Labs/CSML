/******************************************************************************
 * Copyright (c) 2021, Vayavya Labs Pvt. Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Vayavya Labs Pvt. Ltd. nor the
 *    names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VAYAVYA LABS PVT. LTD. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************/

 
#pragma once
#include <functional>
#include <systemc.h>
#include <map>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <vector>
#include <algorithm>
#include <iostream>
#include <string>

#include <csml_report.h>
template <unsigned int>
struct csml_word;

template <>
struct csml_word<32>
{
	unsigned int word;
	typedef unsigned int wordtype;
	typedef unsigned int& reftype;
};

template <>
struct csml_word<16>
{
	unsigned short int word;
	typedef unsigned short int wordtype;
	typedef unsigned short int& reftype;
};

template <>
struct csml_word<8>
{
	unsigned char word;
	typedef unsigned char wordtype;
	typedef unsigned char& reftype;
};

template <>
struct csml_word<64>
{
	unsigned long long word;
	typedef unsigned long long wordtype;
	typedef unsigned long long& reftype;
};

template <unsigned int N>
struct csml_memory
{
	typedef typename csml_word<N>::wordtype DT;
	std::function<void(unsigned int, DT)> write_transport_function;
	std::function<void(unsigned int, DT &)> read_transport_function;

	std::map<unsigned int, std::function<bool(DT)>> write_callbacks;  /* std::function */
	std::map<unsigned int, std::function<bool(DT &)>> read_callbacks; /* std::function */
	std::multimap<unsigned int, std::function<bool()>> post_write_callbacks;
	std::map<unsigned int, std::function<bool(DT, uint8_t)>> write_callbacks_with_be;  /* byte-enable aware callbacks */

	csml_memory(std::string memory_name_, size_t memory_size_) : memory_name(memory_name_), memory_block(memory_size_)
	{
		std::for_each(memory_block.begin(), memory_block.end(), [&](DT &word)
					  { word = 0; });
	}

	void bind_to_socket(tlm_utils::simple_target_socket<csml_memory<N>, 32> &sock)
	{
		sock.register_b_transport(this, &csml_memory<N>::b_transport);
		sock.register_transport_dbg(this, &csml_memory<N>::transport_dbg);
	}

	void b_transport(tlm::tlm_generic_payload &trans, sc_time &delay)
	{
		tlm::tlm_command cmd = trans.get_command();
		if (cmd == tlm::TLM_READ_COMMAND)
		{
			read_registers(trans, false);
		}
		else if (cmd == tlm::TLM_WRITE_COMMAND)
		{
			write_registers(trans, false);
		}
		trans.set_response_status(tlm::TLM_OK_RESPONSE);
	}

	unsigned int transport_dbg(tlm::tlm_generic_payload &trans)
	{
		tlm::tlm_command cmd = trans.get_command();
		if (cmd == tlm::TLM_READ_COMMAND)
		{
			read_registers(trans, true);
		}
		else if (cmd == tlm::TLM_WRITE_COMMAND)
		{
			write_registers(trans, true);
		}
		trans.set_response_status(tlm::TLM_OK_RESPONSE);
		return trans.get_data_length();
	}

	void register_write_callback(std::function<bool(DT)> write_callback_func, unsigned int offset)
	{
		if (write_callbacks.count(offset) != 0)
		{
			write_callbacks.erase(offset);
		}
		write_callbacks.insert({offset, write_callback_func});
	}

	void register_read_callback(std::function<bool(DT &)> read_callback_func, unsigned int offset)
	{
		if (read_callbacks.count(offset) != 0)
		{
			read_callbacks.erase(offset);
		}
		read_callbacks.insert({offset, read_callback_func});
	}

	void register_post_write_callback(std::function<bool()> post_write_callback_func, unsigned int offset)
	{
		post_write_callbacks.insert({offset, post_write_callback_func});
	}

	void register_write_callback_with_be(std::function<bool(DT, uint8_t)> write_callback_func, unsigned int offset)
	{
		if (write_callbacks_with_be.count(offset) != 0)
		{
			write_callbacks_with_be.erase(offset);
		}
		write_callbacks_with_be.insert({offset, write_callback_func});
	}

	void write_registers(tlm::tlm_generic_payload &trans, bool is_debug)
	{
		bool write_status = false;
		sc_dt::uint64 adr = trans.get_address();
		unsigned char *ptr = trans.get_data_ptr();
		unsigned char *be_ptr = trans.get_byte_enable_ptr();
		unsigned int len = trans.get_data_length();

		// Calculate starting word offset and byte offset within that word
		// This supports unaligned addresses (e.g., 0x40001002)
		unsigned int byte_offset_in_word = adr % sizeof(DT);
		unsigned int word_offset = adr / sizeof(DT);

		// Track position in data buffer
		unsigned int data_index = 0;
		unsigned int remaining_bytes = len;

		// Process transaction word by word, handling unaligned/partial accesses
		while (remaining_bytes > 0)
		{
			// Calculate how many bytes to process in current word
			// Can't exceed: (a) remaining bytes, or (b) bytes left in current word
			unsigned int bytes_in_this_word = std::min(
				(unsigned int)sizeof(DT) - byte_offset_in_word,
				remaining_bytes
			);

			// Assemble write value from data buffer
			// Only populate bytes that are actually being written
			DT write_value = 0;
			for (unsigned int i = 0; i < bytes_in_this_word; ++i) {
				unsigned int bit_position = (byte_offset_in_word + i) * 8;
				write_value |= ((DT)ptr[data_index + i] << bit_position);
			}

			// Calculate byte enable mask for this word
			uint8_t byte_enable = 0;
			if (be_ptr != nullptr) {
				// Explicit byte enables provided by transaction
				for (unsigned int i = 0; i < bytes_in_this_word; ++i) {
					if (be_ptr[data_index + i] != 0) {
						byte_enable |= (1 << (byte_offset_in_word + i));
					}
				}
			} else {
				// No explicit byte enables - generate from length/offset
				// Enable only the bytes being written
				for (unsigned int i = 0; i < bytes_in_this_word; ++i) {
					byte_enable |= (1 << (byte_offset_in_word + i));
				}
			}

			// Check byte-enable aware callback first (new style)
			if (write_callbacks_with_be.count(word_offset) != 0)
			{
				try
				{
					write_status = write_callbacks_with_be[word_offset](write_value, byte_enable);
				}
				catch (const std::exception &e)
				{
					CSML_REPORT(WARNING, "MODEL_ERROR",
								"Write callback with byte-enable not properly registered for offset ",
								word_offset * sizeof(DT));
				}

				if (write_status == true)
				{
					// Execute post-write callbacks if registered
					if (post_write_callbacks.count(word_offset) != 0)
					{
						try
						{
							bool post_write_status = false;
							for (auto itr = post_write_callbacks.begin();
								 itr != post_write_callbacks.end();
								 ++itr)
							{
								if (itr->first == word_offset)
								{
									post_write_status = itr->second();
									if (post_write_status == false)
									{
										CSML_REPORT(INFO, "POST_WRITE", "Post write failed");
									}
								}
							}
						}
						catch (const std::exception &e)
						{
							CSML_REPORT(WARNING, "MODEL_ERROR",
										"Post-write callback not properly registered for offset ",
										word_offset * sizeof(DT));
						}
					}
				}
			}
			// Fall back to legacy callback (old style - no byte-enable info)
			else if (write_callbacks.count(word_offset) != 0)
			{
				try
				{
					write_status = write_callbacks[word_offset](write_value);
				}
				catch (const std::exception &e)
				{
					CSML_REPORT(WARNING, "MODEL_ERROR",
								"Write callback not properly registered for offset ",
								word_offset * sizeof(DT));
				}

				if (write_status == true)
				{
					if (post_write_callbacks.count(word_offset) != 0)
					{
						try
						{
							bool post_write_status = false;
							for (auto itr = post_write_callbacks.begin();
								 itr != post_write_callbacks.end();
								 ++itr)
							{
								if (itr->first == word_offset)
								{
									post_write_status = itr->second();
									if (post_write_status == false)
									{
										CSML_REPORT(INFO, "POST_WRITE", "Post write failed");
									}
								}
							}
						}
						catch (const std::exception &e)
						{
							CSML_REPORT(WARNING, "MODEL_ERROR",
										"Post-write callback not properly registered for offset ",
										word_offset * sizeof(DT));
						}
					}
				}
			}
			else
			{
				CSML_REPORT(WARNING, "WRITE_ERROR",
							"RESERVED LOCATION ", word_offset * sizeof(DT));
			}

			// Move to next word
			remaining_bytes -= bytes_in_this_word;
			data_index += bytes_in_this_word;
			word_offset++;
			byte_offset_in_word = 0;  // Subsequent words start at byte 0
		}
	}

	void read_registers(tlm::tlm_generic_payload &trans, bool is_debug)
	{
		bool read_status = false;
		sc_dt::uint64 adr = trans.get_address();
		unsigned char *ptr = trans.get_data_ptr();
		unsigned int len = trans.get_data_length();

		// Calculate starting word offset and byte offset within that word
		// This supports unaligned addresses (e.g., 0x40001002)
		unsigned int byte_offset_in_word = adr % sizeof(DT);
		unsigned int word_offset = adr / sizeof(DT);

		// Track position in data buffer
		unsigned int data_index = 0;
		unsigned int remaining_bytes = len;

		// Process transaction word by word, handling unaligned/partial accesses
		while (remaining_bytes > 0)
		{
			// Calculate how many bytes to read from current word
			unsigned int bytes_in_this_word = std::min(
				(unsigned int)sizeof(DT) - byte_offset_in_word,
				remaining_bytes
			);

			if (read_callbacks.count(word_offset) != 0)
			{
				DT read_value = 0;
				try
				{
					read_status = read_callbacks[word_offset](read_value);
				}
				catch (const std::exception &e)
				{
					CSML_REPORT(WARNING, "MODEL_ERROR",
								"Read callback not properly registered for offset ",
								word_offset * sizeof(DT));
				}

				if (read_status == true)
				{
					// Extract only the requested bytes from the word
					for (unsigned int i = 0; i < bytes_in_this_word; ++i)
					{
						unsigned int bit_position = (byte_offset_in_word + i) * 8;
						ptr[data_index + i] = (read_value >> bit_position) & 0xFF;
					}
				}
			}
			else
			{
				CSML_REPORT(WARNING, "READ_ERROR",
							"RESERVED LOCATION ", word_offset * sizeof(DT));
			}

			// Move to next word
			remaining_bytes -= bytes_in_this_word;
			data_index += bytes_in_this_word;
			word_offset++;
			byte_offset_in_word = 0;  // Subsequent words start at byte 0
		}
	}

	unsigned int get_size() {
		return memory_block.size();
	}

	std::string memory_name;
	std::vector<DT> memory_block;
};

template <unsigned int N>
class csml_reg
{
public:
	typedef csml_memory<N> memory_type;
	typedef typename csml_word<N>::wordtype DT;
	typedef typename csml_word<N>::reftype  RT;

protected:
	csml_reg(std::string name, memory_type &memory, unsigned int offset_, DT read_bit_mask_ = 0, DT write_bit_mask_ = 0, DT reset_value_ = 0) : register_name(name), word_ref(memory.memory_block[offset_]), read_bit_mask(read_bit_mask_), write_bit_mask(write_bit_mask_), reset_value(reset_value_), offset(offset_)
	{
		//std::cout << "Register " << name << " address " << offset_ << " memory size " << memory.get_size() << std::endl;
		memory.register_read_callback(std::bind(&csml_reg<N>::handle_read, this, std::placeholders::_1, this->read_bit_mask), offset);
		memory.register_write_callback(std::bind(&csml_reg<N>::handle_write, this, std::placeholders::_1, this->write_bit_mask), offset);
		csml_reg::reset();
	}
	csml_reg(const csml_reg &) = delete;
	csml_reg(csml_reg &&) = delete;
	csml_reg &operator=(csml_reg &&) = delete;
	~csml_reg() = default;

protected:
	std::string register_name;
	RT word_ref;
public:
	DT read_bit_mask;
	DT write_bit_mask;
	DT reset_value;
	unsigned int offset;

public:
   RT get_word_reference() { return word_ref; }

	operator DT() const { return word_ref; }
	csml_reg &operator=(DT value)
	{
		word_ref = value;
		return *this;
	}
	csml_reg &operator=(const csml_reg &r)
	{
		word_ref = r.word_ref;
		return *this;
	}
	csml_reg &operator+=(DT value)
	{
		word_ref += value;
		return *this;
	}
	csml_reg &operator-=(DT value)
	{
		word_ref -= value;
		return *this;
	}
	csml_reg &operator/=(DT value)
	{
		word_ref /= value;
		return *this;
	}
	csml_reg &operator*=(DT value)
	{
		word_ref *= value;
		return *this;
	}
	csml_reg &operator%=(DT value)
	{
		word_ref %= value;
		return *this;
	}
	csml_reg &operator^=(DT value)
	{
		word_ref ^= value;
		return *this;
	}
	csml_reg &operator&=(DT value)
	{
		word_ref &= value;
		return *this;
	}
	csml_reg &operator|=(DT value)
	{
		word_ref |= value;
		return *this;
	}
	csml_reg &operator>>=(DT value)
	{
		word_ref >>= value;
		return *this;
	}
	csml_reg &operator<<=(DT value)
	{
		word_ref <<= value;
		return *this;
	}
	csml_reg &operator--()
	{
		word_ref = word_ref - 1;
		return *this;
	}
	DT operator--(int)
	{
		DT temp = word_ref;
		word_ref--;
		return temp;
	}
	csml_reg &operator++()
	{
		word_ref = word_ref + 1;
		return *this;
	}
	DT operator++(int)
	{
		DT temp = word_ref;
		word_ref++;
		return temp;
	}
	virtual void reset() {
		word_ref = reset_value;
	}
	virtual bool handle_write(DT value, DT bitmask)
	{
		word_ref = (value & bitmask) | (word_ref & (~bitmask));
		return true;
	}
	virtual bool handle_read(DT &value, DT bitmask)
	{
		value = word_ref & bitmask;
		return true;
	}
	virtual bool handle_read_restriction_error(DT &value)
	{
		value = 0;
		CSML_REPORT(WARNING, "READ_ERROR", this->register_name, " is write only register");
		return false;
	}

	virtual bool handle_write_restriction_error(DT value)
	{
		(void)value;
		CSML_REPORT(WARNING, "WRITE_ERROR", this->register_name, " is read only register");
		return false;
	}

	void set_read_write_restrictions(memory_type &memory)
	{
		if (read_bit_mask == 0)
		{
			memory.register_read_callback(std::bind(&csml_reg<N>::handle_read_restriction_error, this, std::placeholders::_1), offset);
		}

		if (write_bit_mask == 0)
		{
			memory.register_write_callback(std::bind(&csml_reg<N>::handle_write_restriction_error, this, std::placeholders::_1), offset);
		}
	}
};

template <unsigned int N>
class csml_bitfield
{
	typedef typename csml_word<N>::wordtype DT;
	using RT = csml_reg<N> &;

public:
	csml_bitfield(std::string name, RT reg_reference_, unsigned int startbit_, unsigned int number_of_bits_) :
		bitfield_name(name), word_reference(reg_reference_.get_word_reference()), startbit(startbit_), num_of_bits(number_of_bits_)
	{
		bit_mask = (static_cast<DT>(-1) >> (N - num_of_bits)) << startbit;
	}
	csml_bitfield(csml_bitfield &&) = delete;
	csml_bitfield(const csml_bitfield &) = delete;
	csml_bitfield &operator=(csml_bitfield &&) = delete;
	~csml_bitfield() = default;
	DT get() const { return (word_reference & bit_mask) >> startbit; }
	void set_bits_to_register(DT value, unsigned int bit_offset, unsigned int size)
	{
		word_reference = (word_reference & ~bit_mask) | ((value << bit_offset) & bit_mask);
	}
	void put(DT value) { set_bits_to_register(value, startbit, num_of_bits); }
	void put(const csml_bitfield &b)
	{
		DT temp = b;
		put(temp);
	}
	operator DT() const { return (word_reference & bit_mask) >> startbit; }
	csml_bitfield &operator=(DT value)
	{
		put(value);
		return *this;
	}
	csml_bitfield &operator=(const csml_bitfield &b)
	{
		put(b);
		return *this;
	}
	csml_bitfield &operator+=(DT value)
	{
		put(get() + value);
		return *this;
	}
	csml_bitfield &operator-=(DT value)
	{
		put(get() - value);
		return *this;
	}
	csml_bitfield &operator/=(DT value)
	{
		put(get() / value);
		return *this;
	}
	csml_bitfield &operator*=(DT value)
	{
		put(get() * value);
		return *this;
	}
	csml_bitfield &operator%=(DT value)
	{
		put(get() % value);
		return *this;
	}
	csml_bitfield &operator^=(DT value)
	{
		put(get() ^ value);
		return *this;
	}
	csml_bitfield &operator&=(DT value)
	{
		put(get() & value);
		return *this;
	}
	csml_bitfield &operator|=(DT value)
	{
		put(get() | value);
		return *this;
	}
	csml_bitfield &operator<<=(DT value)
	{
		put(get() << value);
		return *this;
	}
	csml_bitfield &operator>>=(DT value)
	{
		put(get() >> value);
		return *this;
	}
	csml_bitfield &operator--()
	{
		put(get() - 1);
		return *this;
	}
	DT operator--(int)
	{
		DT temp = get();
		put(temp - 1);
		return temp;
	}
	csml_bitfield &operator++()
	{
		put(get() + 1);
		return *this;
	}
	DT operator++(int)
	{
		DT temp = get();
		put(temp + 1);
		return temp;
	}

private:
	std::string bitfield_name;
	typename csml_word<N>::reftype  word_reference;
	unsigned int startbit;
	unsigned int num_of_bits;
	DT bit_mask;
};

template <class T, unsigned int Quantity>
class csml_reg_vector
{
public:
	csml_reg_vector(std::string name, typename T::memory_type &mem, unsigned int offset, unsigned int spacing) : reg_vector(name, mem, offset, spacing),
																												 reg(name + "_" + std::to_string(Quantity - 1), mem, offset + (Quantity - 1) * spacing)
	{
	}

	T &operator[](unsigned int num)
	{
		if (num == (Quantity - 1))
		{
			return reg;
		}
		else if (num < Quantity - 1)
		{
			return reg_vector[num];
		}
		else
		{
			CSML_REPORT(ERROR, "MODEL_ERROR", "Access limit of ", Quantity - 1, " Requested access to ", num);
			throw "Out of bound access";
		}
	}

	const T &operator[](unsigned int num) const
	{
		if (num == (Quantity - 1))
		{
			return reg;
		}
		else if (num < Quantity - 1)
		{
			return reg_vector[num];
		}
		else
		{
			CSML_REPORT(ERROR, "MODEL_ERROR", "Access limit of ", Quantity - 1, ". Requested access to ", num);
			throw "Out of bound access";
		}
	}
	constexpr size_t size() const { return Quantity; } 
	csml_reg_vector<T, Quantity - 1> reg_vector;
	T reg;
};

template <class T>
class csml_reg_vector<T, 1>
{
public:
	csml_reg_vector(std::string name, typename T::memory_type &mem, unsigned int offset, unsigned int spacing) : reg(name + "_" + std::to_string(0), mem, offset)
	{
		(void)spacing;
	}
	const T &operator[](unsigned int) const
	{
		return reg;
	}
	T &operator[](unsigned int)
	{
		return reg;
	}
	constexpr size_t size() const { return 1; }
	T reg;
};

template <class T, unsigned int Quantity1, unsigned int Quantity2>
class csml_reg_2D
{
public:
	csml_reg_2D(std::string name, typename T::memory_type &mem, unsigned int offset, unsigned int stride, unsigned int spacing) : reg_2D_subset(name, mem, offset, stride, spacing),
																																  reg_row_vector(name + "_" + std::to_string(Quantity1 - 1), mem, offset + (Quantity1 - 1) * stride, spacing)
	{
	}
	csml_reg_vector<T, Quantity2> &operator[](unsigned int num)
	{
		if (num == (Quantity1 - 1))
		{
			return reg_row_vector;
		}
		else if (num < Quantity1 - 1)
		{
			return reg_2D_subset[num];
		}
		else
		{
			/* throw exception */
			CSML_REPORT(ERROR, "MODEL_ERROR", "Access limit of ", Quantity1 - 1, ". Requested access to ", num);
			throw "Out of bound access";
		}
	}

	const csml_reg_vector<T, Quantity2> &operator[](unsigned int num) const
	{
		if (num == (Quantity1 - 1))
		{
			return reg_row_vector;
		}
		else if (num < Quantity1 - 1)
		{
			return reg_2D_subset[num];
		}
		else
		{
			CSML_REPORT(ERROR, "MODEL_ERROR", "Access limit of ", Quantity1 - 1, ". Requested access to ", num);
			throw "Out of bound access";
		}
	}
	constexpr size_t size() const { return Quantity1 * Quantity2; }
	constexpr size_t size_row() const { return Quantity1; }
	constexpr size_t size_column() const { return Quantity2; }
	csml_reg_2D<T, Quantity1 - 1, Quantity2> reg_2D_subset;
	csml_reg_vector<T, Quantity2> reg_row_vector;
};

template <class T, unsigned int Quantity>
class csml_reg_2D<T, 1, Quantity>
{
public:
	csml_reg_2D(std::string name, typename T::memory_type &mem, unsigned int offset, unsigned int stride, unsigned int spacing) : reg_row_vector(name + "_" + std::to_string(0), mem, offset, spacing)
	{
		(void)spacing;
	}
	csml_reg_vector<T, Quantity> &operator[](unsigned int)
	{
		return reg_row_vector;
	}

	const csml_reg_vector<T, Quantity> &operator[](unsigned int) const
	{
		return reg_row_vector;
	}
	constexpr size_t size() const { return 1*Quantity; }
	constexpr size_t size_row() const { return 1; }
	constexpr size_t size_column() const { return Quantity; }
	csml_reg_vector<T, Quantity> reg_row_vector;
};

template <unsigned int N>
class csml_register_group
{
public:
	typedef csml_memory<N> memory_type;
	typedef typename csml_word<N>::wordtype DT;
};
