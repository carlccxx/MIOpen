#ifndef _OCL_KERNEL_HPP_
#define _OCL_KERNEL_HPP_

#include <sstream>
#include <vector>
#include "MLOpen.h"

struct LocalMemArg 
{
	LocalMemArg(size_t _size) : size(_size) {}
	size_t GetSize() const { return size; }
	
	private:
	size_t size;
};

class OCLKernel {

	public:
	OCLKernel() {}
	OCLKernel(cl_kernel kernel, 
			std::vector<size_t> ldims,
			std::vector<size_t> gdims) : _kernel(kernel) {
		for(int i = 0; i < ldims.size(); i++) {
			_ldims.push_back(ldims[i]);
			_gdims.push_back(gdims[i]);
		}
	}

	//TODO: when to call the destructor?
//	~OCLKernel() { clReleaseKernel(_kernel); }

	template<typename T, typename... Args>
	mlopenStatus_t SetArgs(int i, const T& first, const Args&... rest);
	template<typename... Args>
	mlopenStatus_t SetArgs(int i, const LocalMemArg &lmem, const Args&... rest);
	mlopenStatus_t SetArgs(int i) {
		return mlopenStatusSuccess;
	}

	mlopenStatus_t run(cl_command_queue &queue,
		const int &work_dim,
		const size_t * global_work_offset,
		const size_t * global_work_dim,
		const size_t * local_work_dim);

	cl_kernel& GetKernel() { return _kernel; } 

	mlopenStatus_t GetKernelName(std::string &kernelName);

	inline const std::vector<size_t>& GetLocalDims() const { return _ldims; }
	inline const std::vector<size_t>& GetGlobalDims() const { return _gdims; }

	private:
	cl_kernel _kernel;
	std::vector<size_t> _ldims;
	std::vector<size_t> _gdims;
};

template<typename T, typename... Args>
mlopenStatus_t OCLKernel::SetArgs(int i, 
		const T& first, 
		const Args&... rest)
{
	cl_int status;

	status = clSetKernelArg(_kernel, i++, sizeof(T), (void *)& first);
	std::stringstream errStream;
	errStream<<"OpenCL error setting kernel argument "<<i;
//	clCheckStatus(status, errStream.str()) ;

	status = SetArgs(i, rest...);
	return mlopenStatusSuccess;

}

template<typename... Args>
mlopenStatus_t OCLKernel::SetArgs(int i, 
		const LocalMemArg &lmem, 
		const Args&... rest)
{
	cl_int status;
	status = clSetKernelArg(_kernel, i++, lmem.GetSize(), NULL);
	std::stringstream errStream;
	errStream<<"OpenCL error setting kernel argument (local memory) "<<i;
	//clCheckStatus(status, errStream.str()) ;
	
	status = SetArgs(i, rest...);
	return mlopenStatusSuccess;

}

#endif // _OCL_KERNEL_HPP_
