// ExtRanking.cpp: определяет точку входа для консольного приложения.
//

#include "stdafx.h"
#include <string>
#include <algorithm> 
#include <iostream>
#include <random>
#include <ctime>

//TODO join_and_delete can write data more rare
//TODO share list of temp files during sort
//TODO "eat" down steps, which led to no nodes removal. But doesn't help dues to allocator speed issues:(
//TODO sort can write into original file.

typedef unsigned int UINT;

static const UINT buffer_size = 768 * 640; //84 is min
static UINT* global_buffer = new UINT[buffer_size / sizeof(UINT)];

namespace temp {
	static FILE* shared_temp_file1 = NULL;
	static const char* shared_temp_file_name1 = "temp1";

	static FILE* shared_temp_file2 = NULL;
	static const char* shared_temp_file_name2 = "temp2";

	void create_shared_temp_file() {
		errno_t err = 0;

		err = fopen_s(&shared_temp_file1, shared_temp_file_name1, "w+b");
		if (err != 0) {
			printf_s("Couldn't create shared temp file 1\n");
			return;
		}

		err = fopen_s(&shared_temp_file2, shared_temp_file_name2, "w+b");
		if (err != 0) {
			printf_s("Couldn't create shared temp file 2\n");
			return;
		}
	}
}

namespace util {

	const std::string create_filename(const char* base, const char* suffix) {
		return std::string(base) + std::string(suffix);
	}
}

namespace comp {
	int compare1(const void * a, const void * b)
	{
		if (*(UINT*)a > *(UINT*)b)
		{
			return 1;
		}
		else if (*(UINT*)a < *(UINT*)b)
		{
			return -1;
		}
		else
		{
			return 0;
		}
	}

	int compare2(const void * a, const void * b)
	{
		if (*((UINT*)a + 1) > *((UINT*)b + 1))
		{
			return 1;
		}
		else if (*((UINT*)a + 1) < *((UINT*)b + 1))
		{
			return -1;
		}
		else
		{
			return 0;
		}
	}

	static UINT offset = 0;

	int compare3(const void * a, const void * b)
	{
		if (*((UINT*)a + 1) - offset > *((UINT*)b + 1) - offset)
		{
			return 1;
		}
		else if (*((UINT*)a + 1) - offset < *((UINT*)b + 1) - offset)
		{
			return -1;
		}
		else
		{
			return 0;
		}
	}
}

namespace sort {
	int create_temp_files(FILE *input, int element_count, size_t element_size, int(*compare)(const void*, const void*)) {
		errno_t err = 0;
		// Reading array size
		UINT count;
		fread(&count, sizeof(UINT), 1, input);

		// Creating temp files
		const UINT buffer_lenght = buffer_size / element_size * element_count;
		const int temp_files_count = element_count * count / buffer_lenght + (((element_count * count) % buffer_lenght) ? 1 : 0);

		UINT *buffer = global_buffer;

		UINT count_left = count * element_count;
		for (int i = 0; i < temp_files_count; ++i, count_left -= buffer_lenght) {
			FILE *temp = NULL;
			err = fopen_s(&temp, (std::string("0_") + std::to_string(i)).c_str(), "wb");
			if (err != 0) {
				printf("Error opening temp file for write\n");
				return 0;
			}

			int lenght = std::min(buffer_lenght, count_left);
			int temp_count = lenght / element_count;
			fwrite(&temp_count, sizeof(int), 1, temp);

			fread(buffer, element_size / element_count, lenght, input);

			qsort(buffer, lenght / element_count, element_size, compare);

			fwrite(buffer, element_size / element_count, lenght, temp);

			fclose(temp);
		}

		return count;
	}

	int create_temp_files(const char* input_filename, int element_count, size_t element_size, int(*compare)(const void*, const void*)) {
		// Opening input file
		FILE *input = NULL;
		errno_t err = 0;
		err = fopen_s(&input, input_filename, "rb");
		if (err != 0) {
			printf("Error opening input file\n");
			return 0;
		}

		int count = create_temp_files(input, element_count, element_size, compare);

		fclose(input);

		return count;
	}

	void merge_two_temp_files(const int deep1, const int deep2, const int id1, const int id2, int element_count, size_t element_size, int(*compare)(const void*, const void*), bool write_size) {
		const int files_count = 2;
		const int buffer_lenght = buffer_size / element_size * element_count;
		const int block_size = buffer_size / (files_count + 1);
		const int block_lenght = block_size / element_size * element_count;

		errno_t err = 0;

		FILE** files = new FILE*[files_count];
		int temp_file_lenghts[files_count];
		int temp_file_positions[files_count];
		UINT** buffers = new UINT*[files_count];

		err = fopen_s(&files[0], (std::to_string(deep1) + std::string("_") + std::to_string(id1)).c_str(), "rb");
		if (err != 0) {
			printf("Error opening temp file for read\n");
			return;
		}
		err = fopen_s(&files[1], (std::to_string(deep2) + std::string("_") + std::to_string(id2)).c_str(), "rb");
		if (err != 0) {
			printf("Error opening temp file for read\n");
			return;
		}

		for (int i = 0; i < files_count; ++i) {
			temp_file_positions[i] = 0;

			fread(&temp_file_lenghts[i], sizeof(int), 1, files[i]);
			temp_file_lenghts[i] *= element_count;

			buffers[i] = global_buffer + (i + 1) * block_lenght;
			fread(buffers[i], element_size / element_count, block_lenght, files[i]);
		}

		// Creating output file
		FILE *output = NULL;
		err = fopen_s(&output, (std::to_string(deep1 + 1) + std::string("_") + std::to_string(id1)).c_str(), "wb");
		if (err != 0) {
			printf("Error opening output file\n");
			return;
		}

		const int count = (temp_file_lenghts[0] + temp_file_lenghts[1]) / element_count;
		if (write_size) {
			fwrite(&count, sizeof(int), 1, output);
		}

		// Merging sorted files
		UINT* write_buffer = global_buffer;
		int write_buffer_position = 0;
		for (int i = 0; i < count; ++i) {
			UINT* min_element = nullptr;
			int min_element_block = -1;

			// Searching for min element value and block number
			for (int j = 0; j < files_count; ++j) {
				if (temp_file_positions[j] == temp_file_lenghts[j]) {
					continue;
				}
				else if (min_element == nullptr) {
					min_element_block = j;
					min_element = &buffers[j][temp_file_positions[j] % block_lenght];
				}
				else if (compare((void*)&buffers[j][temp_file_positions[j] % block_lenght], min_element) < 0)
				{
					min_element = &buffers[j][temp_file_positions[j] % block_lenght];
					min_element_block = j;
				}
			}

			// Put min element into write buffer
			for (int j = 0; j < element_count; ++j) {
				write_buffer[write_buffer_position + j] = min_element[j];
			}
			write_buffer_position += element_count;

			// Flush write buffer if necessary
			if (write_buffer_position == block_lenght) {
				fwrite(write_buffer, element_size / element_count, write_buffer_position, output);
				write_buffer_position = 0;
			}

			// Read new buffer for the min element if neccessary
			temp_file_positions[min_element_block] += element_count;
			if (temp_file_positions[min_element_block] < temp_file_lenghts[min_element_block] &&
				temp_file_positions[min_element_block] % block_lenght == 0) {
				fread(buffers[min_element_block], element_size / element_count, std::min(temp_file_lenghts[min_element_block] - temp_file_positions[min_element_block], block_lenght), files[min_element_block]);
			}
		}

		// Flushing what's left
		if (write_buffer_position != 0) {
			fwrite(write_buffer, element_size / element_count, write_buffer_position, output);
			write_buffer_position = 0;
		}

		fclose(output);

		delete[] buffers;

		for (int i = 0; i < files_count; ++i) {
			fclose(files[i]);
		}
		delete[] files;
	}

	void merge_all_temp_files(const char* output_filename, const int count, int element_count, size_t element_size, int(*compare)(const void*, const void*), bool write_size) {
		const int buffer_lenght = buffer_size / element_size * element_count;
		const int files_count = element_count * count / buffer_lenght + (((element_count * count) % buffer_lenght) ? 1 : 0);

		int* file_indeces = new int[files_count];
		int* file_deeps = new int[files_count];
		for (int i = 0; i < files_count; ++i) {
			file_indeces[i] = i;
			file_deeps[i] = 0;
		}

		int deep = 0;
		int bias = 1;
		int step = 2;
		int processed = 0;
		while (processed != files_count - 1) {
			for (int i = 0; i < files_count; i += step) {
				if (i + bias < files_count) {
					merge_two_temp_files(file_deeps[i], file_deeps[i + bias], file_indeces[i], file_indeces[i + bias], element_count, element_size, compare, write_size || (processed + 1 != files_count - 1));
					file_deeps[i] = deep + 1;
					++processed;
				}
			}

			++deep;
			bias *= 2;
			step *= 2;
		}

		delete[] file_deeps;
		delete[] file_indeces;

		int rename_result = rename((std::to_string(deep) + std::string("_") + std::to_string(0)).c_str(), output_filename);
		if (rename_result == 0) {
			puts("File successfully renamed");
		}
		else {
			perror("Error renaming file");
		}
	}

	void ext_sort(FILE *input, const char* output_filename, int element_count, size_t element_size, int(*compare)(const void*, const void*), bool write_size) {
		int count = create_temp_files(input, element_count, element_size, compare);
		merge_all_temp_files(output_filename, count, element_count, element_size, compare, write_size);
	}

	void ext_sort(const char* input_filename, const char* output_filename, int element_count, size_t element_size, int(*compare)(const void*, const void*), bool write_size) {
		int count = create_temp_files(input_filename, element_count, element_size, compare);
		merge_all_temp_files(output_filename, count, element_count, element_size, compare, write_size);
	}
}

namespace test {
	void create_test_input(const UINT size, const UINT min_element) {
		FILE *output = NULL;

		errno_t err = 0;
		err = fopen_s(&output, "input.bin", "wb");
		if (err != 0) {
			printf("Error opening test file\n");
			return;
		}

		fwrite(&size, sizeof(UINT), 1, output);

		UINT* test_data = new UINT[2 * size];
		for (int i = 0; i < size; ++i) {
			test_data[2 * i] = min_element + i;
			test_data[2 * i + 1] = (i + 1) == size ? min_element : min_element + i + 1;
		}

		fwrite(test_data, sizeof(UINT), 2 * size, output);

		delete[] test_data;
		fclose(output);
	}

	void create_test_input() {
		FILE *output = NULL;

		errno_t err = 0;
		err = fopen_s(&output, "input.bin", "wb");
		if (err != 0) {
			printf("Error opening test file\n");
			return;
		}

		int count = 9;
		fwrite(&count, sizeof(int), 1, output);

		UINT temp = 4;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 3;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 9;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 2;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 3;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 5;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 6;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 7;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 1;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 8;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 7;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 1;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 2;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 6;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 5;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 9;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 8;
		fwrite(&temp, sizeof(UINT), 1, output);
		temp = 4;
		fwrite(&temp, sizeof(UINT), 1, output);

		fclose(output);
	}

	void display_result(const char* filename, int element_count, int count) {
		errno_t err = 0;

		FILE *result = NULL;
		err = fopen_s(&result, filename, "rb");
		if (err != 0) {
			printf("Error opening result file\n");
			return;
		}

		if (count == 0) {
			fread(&count, sizeof(int), 1, result);
			std::cout << count << '\n';
		}

		UINT *element = new UINT[element_count];
		for (int i = 0; i < count; ++i) {

			fread(element, sizeof(UINT), element_count, result);

			for (int j = 0; j < element_count; ++j) {
				std::cout << (UINT)element[j] << " ";
			}
			std::cout << '\n';
		}

		delete[] element;
		fclose(result);
	}
}

namespace prepare {

	int add_weights_to_elements(const char* sorted_elements_filename, const char* output_filename) {
		errno_t err = 0;

		FILE *output = NULL;
		err = fopen_s(&output, output_filename, "wb");
		if (err != 0) {
			printf_s("Couldn't create file for elements with weights\n");
			return -1;
		}

		FILE *elements_input = NULL;
		err = fopen_s(&elements_input, sorted_elements_filename, "rb");
		if (err != 0) {
			printf("Error opening elements file\n");
			return -1;
		}

		int count;
		fread(&count, sizeof(int), 1, elements_input);
		fwrite(&count, sizeof(UINT), 1, output);

		const int base_buffer_lenght = buffer_size / 5 / sizeof(UINT);

		UINT* input_buffer = global_buffer;
		UINT* output_buffer = global_buffer + base_buffer_lenght * 2;

		for (int i = 0; i < count; i += base_buffer_lenght) {
			const UINT process_count = std::min(base_buffer_lenght, count - i);

			fread(input_buffer, sizeof(UINT), process_count * 2, elements_input);

			for (int j = 0; j < process_count; ++j) {
				output_buffer[j * 3] = input_buffer[j * 2];
				output_buffer[j * 3 + 1] = input_buffer[j * 2 + 1];
				output_buffer[j * 3 + 2] = 1;
			}

			fwrite(output_buffer, sizeof(UINT), process_count * 3, output);
		}

		fclose(elements_input);
		fclose(output);

		return count;
	}

	int add_weights_and_sort(const char* input_filename, const char* output_filename) {
		sort::ext_sort(input_filename, "sorted_input", 2, 2 * sizeof(UINT), comp::compare1, true);
		return add_weights_to_elements("sorted_input", output_filename);
	}
}

namespace ranking {

	void mark_elements_to_delete(FILE *input, FILE *output) {
		int count;
		fread(&count, sizeof(int), 1, input);
		fwrite(&count, sizeof(UINT), 1, output);

		const int base_buffer_lenght = buffer_size / 7 / sizeof(UINT);

		UINT* input_buffer = global_buffer;
		UINT* output_buffer = global_buffer + base_buffer_lenght * 3;

		for (int i = 0; i < count; i += base_buffer_lenght) {
			const UINT process_count = std::min(base_buffer_lenght, count - i);

			fread(input_buffer, sizeof(UINT), process_count * 3, input);

			for (int j = 0; j < process_count; ++j) {
				output_buffer[j * 4] = input_buffer[j * 3];
				output_buffer[j * 4 + 1] = input_buffer[j * 3 + 1];
				output_buffer[j * 4 + 2] = input_buffer[j * 3 + 2];
				output_buffer[j * 4 + 3] = std::rand() % 2;
			}

			fwrite(output_buffer, sizeof(UINT), process_count * 4, output);
		}
	}

	void mark_elements_to_delete(const char* input_filename, const char* output_filename) {
		errno_t err = 0;

		FILE *output = NULL;
		err = fopen_s(&output, output_filename, "wb");
		if (err != 0) {
			printf_s("Couldn't create file for marked elements\n");
			return;
		}

		FILE *elements_input = NULL;
		err = fopen_s(&elements_input, input_filename, "rb");
		if (err != 0) {
			printf("Error opening weighted elements file\n");
			return;
		}

		mark_elements_to_delete(elements_input, output);

		fclose(elements_input);
		fclose(output);
	}

	UINT join_and_delete(FILE *input1, FILE *input2, FILE *output) {
		int count1;
		fread(&count1, sizeof(int), 1, input1);

		int count2;
		fread(&count2, sizeof(int), 1, input2);

		if (count1 != count2) {
			printf("Can't merge lists of different lenght\n");
			return -1;
		}

		const int base_buffer_lenght = buffer_size / 11 / sizeof(UINT);

		UINT* input_buffer1 = global_buffer;
		UINT* input_buffer2 = global_buffer + base_buffer_lenght * 4;
		UINT* output_buffer = global_buffer + base_buffer_lenght * 8;

		int total_count_left = 0;
		fwrite(&total_count_left, sizeof(UINT), 1, output);

		int count = count1;
		for (int i = 0; i < count; i += base_buffer_lenght) {
			const UINT process_count = std::min(base_buffer_lenght, count - i);

			fread(input_buffer1, sizeof(UINT), base_buffer_lenght * 4, input1);
			fread(input_buffer2, sizeof(UINT), base_buffer_lenght * 4, input2);

			int count_left = 0;
			for (int j = 0; j < process_count; ++j) {
				if (input_buffer2[j * 4 + 3] == 0 && input_buffer1[j * 4 + 3] == 0) {
					output_buffer[count_left * 3 + 0] = input_buffer2[j * 4 + 1];
					output_buffer[count_left * 3 + 1] = input_buffer1[j * 4 + 1];
					output_buffer[count_left * 3 + 2] = input_buffer1[j * 4 + 2];
					++count_left;
				}
				else if (input_buffer2[j * 4 + 3] == 0 && input_buffer1[j * 4 + 3] == 1) {
					continue;
				}
				else if (input_buffer2[j * 4 + 3] == 1 && input_buffer1[j * 4 + 3] == 0) {
					output_buffer[count_left * 3 + 0] = input_buffer2[j * 4 + 0];
					output_buffer[count_left * 3 + 1] = input_buffer1[j * 4 + 1];
					output_buffer[count_left * 3 + 2] = input_buffer2[j * 4 + 2] + input_buffer1[j * 4 + 2];
					++count_left;
				}
				else {
					output_buffer[count_left * 3 + 0] = input_buffer2[j * 4 + 0];
					output_buffer[count_left * 3 + 1] = input_buffer2[j * 4 + 1];
					output_buffer[count_left * 3 + 2] = input_buffer2[j * 4 + 2];
					++count_left;
				}
			}

			total_count_left += count_left;

			fwrite(output_buffer, sizeof(UINT), count_left * 3, output);
		}

		rewind(output);
		fwrite(&total_count_left, sizeof(UINT), 1, output);

		return total_count_left;
	}

	UINT join_and_delete(const char* input1_filename, const char* input2_filename, const char* output_filename) {
		errno_t err = 0;

		FILE *input1 = NULL;
		err = fopen_s(&input1, input1_filename, "rb");
		if (err != 0) {
			printf("Error opening input1 file\n");
			return -1;
		}

		FILE *input2 = NULL;
		err = fopen_s(&input2, input2_filename, "rb");
		if (err != 0) {
			printf("Error opening input2 file\n");
			return -1;
		}

		FILE *output = NULL;
		err = fopen_s(&output, output_filename, "wb");
		if (err != 0) {
			printf("Error opening join file\n");
			return -1;
		}

		UINT total_count_left = join_and_delete(input1, input2, output);

		fclose(input1);
		fclose(input2);
		fclose(output);

		return total_count_left;
	}

	int recursion_down_step(const char* sorted_input_filename, const char* sorted_output_filename, UINT count) {
		const std::string marked_file_name = util::create_filename(sorted_input_filename, "_m");
		const std::string marked_file_name_2 = util::create_filename(marked_file_name.c_str(), "_2");
		const std::string self_joined_file_name = util::create_filename(sorted_input_filename, "_sj");
		const std::string not_sorted_output_filename = util::create_filename(self_joined_file_name.c_str(), "_nso");

		UINT count_after_deletion = count;
		
		{
			// mark items in sorted input for deletion
			FILE *sorted_input_file = NULL;
			fopen_s(&sorted_input_file, sorted_input_filename, "rb");
			rewind(temp::shared_temp_file1);
			mark_elements_to_delete(sorted_input_file, temp::shared_temp_file1);
			fclose(sorted_input_file);

			std::cout << "Marked to delete (previously sorted):\n";
			//test::display_result(marked_file_name.c_str(), 4, 0);
		}

		{
			rewind(temp::shared_temp_file1);
			sort::ext_sort(temp::shared_temp_file1, marked_file_name_2.c_str(), 4, 4 * sizeof(UINT), comp::compare2, true);

			//test::display_result(marked_file_name.c_str(), 4, 0);
			//test::display_result(marked_file_name_2.c_str(), 4, 0);
		}

		{
			FILE *marked_file_2 = NULL;
			fopen_s(&marked_file_2, marked_file_name_2.c_str(), "rb");
			rewind(temp::shared_temp_file1);
			rewind(temp::shared_temp_file2);
			count_after_deletion = join_and_delete(temp::shared_temp_file1, marked_file_2, temp::shared_temp_file2);
			//test::display_result(not_sorted_output_filename.c_str(), 3, 0);
			fclose(marked_file_2);

			rewind(temp::shared_temp_file2);
			sort::ext_sort(temp::shared_temp_file2, sorted_output_filename, 3, 3 * sizeof(UINT), comp::compare1, true);
		}


		return count_after_deletion;
	}

	void rank_list(const char* input_filename, const char* output_filename) {
		errno_t err = 0;

		FILE *input = NULL;
		err = fopen_s(&input, input_filename, "rb");
		if (err != 0) {
			printf("Error opening weighted elements file 1\n");
			return;
		}

		UINT count;
		fread(&count, sizeof(UINT), 1, input);

		const UINT base_buffer_lenght = std::min(buffer_size / 7 / ((UINT)sizeof(UINT)), count);

		UINT* input_buffer = global_buffer;
		fread(input_buffer, sizeof(UINT), base_buffer_lenght * 3, input);

		fclose(input);

		UINT* output_buffer = global_buffer + base_buffer_lenght * 3;

		UINT query_element = input_buffer[0];
		UINT rank = 0;
		for (UINT i = 0; i < count; ++i) {
			for (UINT j = 0; j < count; ++j) {
				if (input_buffer[j * 3] == query_element) {
					query_element = input_buffer[j * 3 + 1];
					rank += input_buffer[j * 3 + 2];
					output_buffer[i * 4] = input_buffer[j * 3];
					output_buffer[i * 4 + 1] = input_buffer[j * 3 + 1];
					output_buffer[i * 4 + 2] = input_buffer[j * 3 + 2];
					output_buffer[i * 4 + 3] = rank;
					break;
				}
			}
		}


		FILE *output = NULL;
		err = fopen_s(&output, output_filename, "wb");
		if (err != 0) {
			printf("Error opening weighted elements file 1\n");
			return;
		}
		fwrite(&count, sizeof(UINT), 1, output);
		fwrite(output_buffer, sizeof(UINT), base_buffer_lenght * 4, output);

		fclose(output);
	}

	void merge_and_rank(const char* input1_filename, const char* input2_filename, const char* output_filename) {
		errno_t err = 0;

		FILE *input1 = NULL;
		err = fopen_s(&input1, input1_filename, "rb");
		if (err != 0) {
			printf("Error opening file from recursion down file\n");
			return;
		}

		int count1;
		fread(&count1, sizeof(int), 1, input1);

		FILE *input2 = NULL;
		err = fopen_s(&input2, input2_filename, "rb");
		if (err != 0) {
			printf("Error opening ranked list file\n");
			return;
		}
		int count2;
		fread(&count2, sizeof(int), 1, input2);

		FILE *output = NULL;
		err = fopen_s(&output, output_filename, "wb");
		if (err != 0) {
			printf("Error opening join file\n");
			return;
		}
		fwrite(&count1, sizeof(UINT), 1, output);

		const int base_buffer_lenght = buffer_size / 11 / sizeof(UINT);

		UINT* input_buffer1 = global_buffer;
		UINT* input_buffer2 = global_buffer + base_buffer_lenght * 3;
		UINT* output_buffer = global_buffer + base_buffer_lenght * 7;

		fread(input_buffer2, sizeof(UINT), base_buffer_lenght * 4, input2);


		int processed_count = 0;
		int output_buffer_lenght = 0;
		for (int i = 0, j = 0; j < count2; ++i) {
			const int buffer_i = i % base_buffer_lenght; //changed all i to it
			const int buffer_j = j % base_buffer_lenght; // changed all j to it

														 //TODO: read new buffers if reached limit
			if (buffer_i == 0) {
				fread(input_buffer1, sizeof(UINT), base_buffer_lenght * 3, input1);
			}

			if (input_buffer1[buffer_i * 3] == input_buffer2[buffer_j * 4]) {
				if (input_buffer1[buffer_i * 3 + 1] == input_buffer2[buffer_j * 4 + 1]) {
					output_buffer[output_buffer_lenght * 4] = input_buffer2[buffer_j * 4];
					output_buffer[output_buffer_lenght * 4 + 1] = input_buffer2[buffer_j * 4 + 1];
					output_buffer[output_buffer_lenght * 4 + 2] = input_buffer2[buffer_j * 4 + 2];
					output_buffer[output_buffer_lenght * 4 + 3] = input_buffer2[buffer_j * 4 + 3];

					++processed_count;
					++output_buffer_lenght;
					if (output_buffer_lenght == base_buffer_lenght) {
						fwrite(output_buffer, sizeof(UINT), output_buffer_lenght * 4, output);
						output_buffer_lenght = 0;
					}
				}
				else {
					output_buffer[output_buffer_lenght * 4] = input_buffer1[buffer_i * 3];
					output_buffer[output_buffer_lenght * 4 + 1] = input_buffer1[buffer_i * 3 + 1];
					output_buffer[output_buffer_lenght * 4 + 2] = input_buffer1[buffer_i * 3 + 2];
					output_buffer[output_buffer_lenght * 4 + 3] = input_buffer2[buffer_j * 4 + 3] - input_buffer2[buffer_j * 4 + 2] + input_buffer1[buffer_i * 3 + 2];

					++processed_count;
					++output_buffer_lenght;
					if (output_buffer_lenght == base_buffer_lenght) {
						fwrite(output_buffer, sizeof(UINT), output_buffer_lenght * 4, output);
						output_buffer_lenght = 0;
					}

					output_buffer[output_buffer_lenght * 4] = input_buffer1[buffer_i * 3 + 1];
					output_buffer[output_buffer_lenght * 4 + 1] = input_buffer2[buffer_j * 4 + 1];
					output_buffer[output_buffer_lenght * 4 + 2] = input_buffer2[buffer_j * 4 + 2] - input_buffer1[buffer_i * 3 + 2];
					output_buffer[output_buffer_lenght * 4 + 3] = input_buffer2[buffer_j * 4 + 3];

					++processed_count;
					++output_buffer_lenght;
					if (output_buffer_lenght == base_buffer_lenght) {
						fwrite(output_buffer, sizeof(UINT), output_buffer_lenght * 4, output);
						output_buffer_lenght = 0;
					}
				}
				++j;

				// Just added
				if (j % base_buffer_lenght == 0) { // but, we're reading it too often
					fread(input_buffer2, sizeof(UINT), base_buffer_lenght * 4, input2);
				}
			}
		}

		if (output_buffer_lenght != 0) {
			fwrite(output_buffer, sizeof(UINT), output_buffer_lenght * 4, output);
		}

		rewind(output);
		fwrite(&processed_count, sizeof(UINT), 1, output);

		fclose(input1);
		fclose(input2);
		fclose(output);
	}

	void recursion_up_step(const char* sorted_input1_filename, const char* sorted_input2_filename, const char* sorted_output_filename) {
		//recently added
		//test::display_result(sorted_input2_filename, 4, 0);
		//test::display_result(sorted_input1_filename, 3, 0);

		std::string not_sorted_output_filename = util::create_filename(sorted_output_filename, "_n_s");
		merge_and_rank(sorted_input1_filename, sorted_input2_filename, not_sorted_output_filename.c_str());
		//test::display_result(not_sorted_output_filename.c_str(), 4, 0);

		sort::ext_sort(not_sorted_output_filename.c_str(), sorted_output_filename, 4, 4 * sizeof(UINT), comp::compare1, true);
	}

	UINT find_rank_of_min_and_strip(const char* input_filename, const char* output_filename) {
		errno_t err = 0;

		FILE *output = NULL;
		err = fopen_s(&output, output_filename, "wb");
		if (err != 0) {
			printf_s("Couldn't create file for stripped elements\n");
			return -1;
		}

		FILE *input = NULL;
		err = fopen_s(&input, input_filename, "rb");
		if (err != 0) {
			printf("Error opening not stripped file\n");
			return -1;
		}

		UINT count;
		fread(&count, sizeof(UINT), 1, input);
		fwrite(&count, sizeof(UINT), 1, output);

		const UINT base_buffer_lenght = buffer_size / 6 / sizeof(UINT);

		UINT* input_buffer = global_buffer;
		UINT* output_buffer = global_buffer + base_buffer_lenght * 4;

		UINT rank_of_min = 0;
		UINT min = -1;
		for (int i = 0; i < count; i += base_buffer_lenght) {
			const int process_count = std::min(base_buffer_lenght, count - i);

			fread(input_buffer, sizeof(UINT), process_count * 4, input);

			for (int j = 0; j < process_count; ++j) {
				if (min == -1 || input_buffer[j * 4] < min) {
					min = input_buffer[j * 4];
					rank_of_min = input_buffer[j * 4 + 3];
				}
				output_buffer[j * 2] = input_buffer[j * 4];
				output_buffer[j * 2 + 1] = input_buffer[j * 4 + 3];
			}

			fwrite(output_buffer, sizeof(UINT), process_count * 2, output);
		}

		fclose(input);
		fclose(output);

		return rank_of_min;
	}

	UINT do_answer(const char* input_filename, const char* output_filename) {
		errno_t err = 0;

		FILE *output = NULL;
		err = fopen_s(&output, output_filename, "wb");
		if (err != 0) {
			printf_s("Couldn't create file for stripped elements\n");
			return -1;
		}

		FILE *input = NULL;
		err = fopen_s(&input, input_filename, "rb");
		if (err != 0) {
			printf("Error opening not stripped file\n");
			return -1;
		}

		UINT count;
		fread(&count, sizeof(UINT), 1, input);

		const UINT base_buffer_lenght = buffer_size / 3 / sizeof(UINT);
		UINT* input_buffer = global_buffer;
		UINT* output_buffer = global_buffer + base_buffer_lenght * 2;

		for (int i = 0; i < count; i += base_buffer_lenght) {
			const int process_count = std::min(base_buffer_lenght, count - i);

			fread(input_buffer, sizeof(UINT), process_count * 2, input);

			for (int j = 0; j < process_count; ++j) {
				output_buffer[j] = input_buffer[j * 2];
			}

			fwrite(output_buffer, sizeof(UINT), process_count, output);
		}

		fclose(input);
		fclose(output);

		return count;
	}
}

int main()
{
	// Creating test data
	remove("output.bin");
	remove("temp1.bin");
	remove("temp2.bin");
	remove("join.bin");
	test::create_test_input(2500000, 0);
	std::cout << "Input:\n";
	//test::display_result("input.bin", 2, 0);

	const auto startTime = std::clock();

	// Prepare stage
	temp::create_shared_temp_file();
	UINT sublist_size = prepare::add_weights_and_sort("input.bin", "down_input_0");

	std::cout << "Sorted, ranked, weighted\n";
	//test::display_result("down_input_0", 3, 0);

	// loop
	// Algorithm step
	int deep = 0;
	std::string base_input_filename = std::string("down_input_");
	std::string input_filename = util::create_filename(base_input_filename.c_str(), std::to_string(deep).c_str());
	do {
		++deep;
		std::cout << "Down iteration " << deep << '\n';
		std::string output_filename = util::create_filename(base_input_filename.c_str(), std::to_string(deep).c_str());
		sublist_size = ranking::recursion_down_step(input_filename.c_str(), output_filename.c_str(), sublist_size);
		input_filename = output_filename;

		//test::display_result(output_filename.c_str(), 3, 0);
	} while (sublist_size * 3 * sizeof(UINT) > buffer_size / 7 * 3);

	std::cout << "Ranking!\n";
	{
		ranking::rank_list(input_filename.c_str(), "ranked");
		//test::display_result("ranked", 4, 0);
		sort::ext_sort("ranked", "ranked_sorted", 4, 4 * sizeof(UINT), comp::compare1, true);
		//test::display_result("ranked_sorted", 4, 0);
	}

	//std::cout << "Going up!\n";
	std::string base_output_filename = std::string("up_output_");
	std::string input_filename2 = "ranked_sorted";
	do {
		--deep;
		std::cout << "UP iteration " << deep << '\n';
		std::string input_filename1 = util::create_filename(base_input_filename.c_str(), std::to_string(deep).c_str());
		std::string output_filename = util::create_filename(base_output_filename.c_str(), std::to_string(deep).c_str());
		ranking::recursion_up_step(input_filename1.c_str(), input_filename2.c_str(), output_filename.c_str());
		input_filename2 = output_filename;
		//test::display_result(output_filename.c_str(), 4, 0);
	} while (deep > 0);

	comp::offset = ranking::find_rank_of_min_and_strip(input_filename2.c_str(), "stripped");
	//test::display_result("stripped", 2, 0);

	sort::ext_sort("stripped", "sorted_stripped", 2, 2 * sizeof(UINT), comp::compare3, true);
	//test::display_result("sorted_stripped", 2, 0);

	UINT count = ranking::do_answer("sorted_stripped", "output.bin");
	const auto endTime = std::clock();

	//test::display_result("output.bin", 1, count);

	// Calculating execution time
	double time = double(endTime - startTime) / CLOCKS_PER_SEC;
	std::cerr << "Time: " << time << '\n';

	return 0;
}

