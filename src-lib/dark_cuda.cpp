#include "darknet_internal.hpp"

/// @todo V3 is cuda_debug_sync still necessary?
int cuda_debug_sync = 0;

#ifdef GPU

#include <cuda.h>

#if defined(CUDNN_HALF) && !defined(CUDNN)
#error "If you set CUDNN_HALF=1 then you must set CUDNN=1"
#endif


namespace
{
	static auto & cfg_and_state = Darknet::CfgAndState::get();
}


void cuda_set_device(int n)
{
	TAT(TATPARMS);

	cfg_and_state.gpu_index = n;
	cudaError_t status = cudaSetDevice(n);
	if (status != cudaSuccess)
	{
		CHECK_CUDA(status);
	}
}

int cuda_get_device()
{
	TAT(TATPARMS);

	int n = 0;
	cudaError_t status = cudaGetDevice(&n);
	CHECK_CUDA(status);
	return n;
}

void *cuda_get_context()
{
	TAT(TATPARMS);

	CUcontext pctx;
	CUresult status = cuCtxGetCurrent(&pctx);
	if(status != CUDA_SUCCESS) fprintf(stderr, " Error: cuCtxGetCurrent() is failed \n");
	return (void *)pctx;
}

void check_cuda_error(cudaError_t status, const char * const filename, const char * const funcname, const int line)
{
	TAT_REVIEWED(TATPARMS, "2024-05-02");

	if (status != cudaSuccess)
	{
		darknet_fatal_error(filename, funcname, line, "current CUDA error: status=%d %s: %s", status, cudaGetErrorName(status), cudaGetErrorString(status));
	}

	status = cudaGetLastError();
	if (status != cudaSuccess)
	{
		darknet_fatal_error(filename, funcname, line, "most recent CUDA error: status=%d %s: %s", status, cudaGetErrorName(status), cudaGetErrorString(status));
	}
}

void check_cuda_error_extended(cudaError_t status, const char * const filename, const char * const funcname, const int line)
{
	TAT_REVIEWED(TATPARMS, "2024-05-02");

	if (status != cudaSuccess)
	{
		std::cout << "CUDA status error: " << filename << ", " << funcname << "(), line #" << line << std::endl;
		check_cuda_error(status, filename, funcname, line);
	}

#if defined(DEBUG) || defined(CUDA_DEBUG)
	cuda_debug_sync = 1;
#endif

	if (cuda_debug_sync)
	{
		status = cudaDeviceSynchronize();
		if (status != cudaSuccess)
		{
			std::cout << "CUDA status = cudaDeviceSynchronize() error: " << filename << ", " << funcname << "(), line #" << line << std::endl;
		}
	}
	check_cuda_error(status, filename, funcname, line);
}

dim3 cuda_gridsize(size_t n)
{
	size_t k = (n-1) / BLOCK + 1;
	size_t x = k;
	size_t y = 1;
	if(x > 65535){
		x = ceil(sqrt(k));
		y = (n-1)/(x*BLOCK) + 1;
	}
	//dim3 d = { (unsigned int)x, (unsigned int)y, 1 };
	dim3 d;
	d.x = x;
	d.y = y;
	d.z = 1;
	//printf("%ld %ld %ld %ld\n", n, x, y, x*y*BLOCK);
	return d;
}

static cudaStream_t streamsArray[16];    // cudaStreamSynchronize( get_cuda_stream() );
static int streamInit[16] = { 0 };

cudaStream_t get_cuda_stream()
{
	TAT(TATPARMS);

	int i = cuda_get_device();
	if (!streamInit[i])
	{
		if (cfg_and_state.is_trace)
		{
			std::cout << "create CUDA stream for device #" << i << std::endl;
		}
#ifdef CUDNN
		cudaError_t status = cudaStreamCreateWithFlags(&streamsArray[i], cudaStreamNonBlocking);
#else
		cudaError_t status = cudaStreamCreate(&streamsArray[i]);
#endif
		if (status != cudaSuccess)
		{
			fflush(NULL);
			printf("\ncudaStreamCreate error: %d\n", status);
			const char *s = cudaGetErrorString(status);
			printf("CUDA Error: %s\n", s);
			status = cudaStreamCreateWithFlags(&streamsArray[i], cudaStreamNonBlocking);    // cudaStreamDefault
			CHECK_CUDA(status);
		}
		streamInit[i] = 1;
	}
	return streamsArray[i];
}

/*
static cudaStream_t streamsArray2[16];    // cudaStreamSynchronize( get_cuda_memcpy_stream() );
static int streamInit2[16] = { 0 };

cudaStream_t get_cuda_memcpy_stream() {
	int i = cuda_get_device();
	if (!streamInit2[i]) {
		printf(" Create COPY stream %d \n", i);
		//cudaError_t status = cudaStreamCreate(&streamsArray2[i], cudaStreamNonBlocking);
		cudaError_t status = cudaStreamCreateWithFlags(&streamsArray2[i], cudaStreamNonBlocking);
		if (status != cudaSuccess) {
			printf(" cudaStreamCreate-Memcpy error: %d \n", status);
			const char *s = cudaGetErrorString(status);
			printf("CUDA Error: %s\n", s);
			status = cudaStreamCreateWithFlags(&streamsArray2[i], cudaStreamNonBlocking);
			CHECK_CUDA(status);
		}
		streamInit2[i] = 1;
	}
	return streamsArray2[i];
}
*/

#ifdef CUDNN
static int cudnnInit[16] = { 0 };
static cudnnHandle_t cudnnHandle[16];

cudnnHandle_t cudnn_handle()
{
	TAT(TATPARMS);

	int i = cuda_get_device();
	if(!cudnnInit[i])
	{
		cudnnCreate(&cudnnHandle[i]);
		cudnnInit[i] = 1;
		cudnnStatus_t status = cudnnSetStream(cudnnHandle[i], get_cuda_stream());
		CHECK_CUDNN(status);

		if (cfg_and_state.is_trace)
		{
			std::cout << "create cuDNN handle for device #" << i << std::endl;
		}
	}
	return cudnnHandle[i];
}


void cudnn_check_error(cudnnStatus_t status, const char * const filename, const char * const function, const int line)
{
	TAT(TATPARMS);

#if defined(DEBUG) || defined(CUDA_DEBUG)
	cudaDeviceSynchronize();
#endif
	if (cuda_debug_sync) {
		cudaDeviceSynchronize();
	}
	cudnnStatus_t status2 = CUDNN_STATUS_SUCCESS;
#ifdef CUDNN_ERRQUERY_RAWCODE
	cudnnStatus_t status_tmp = cudnnQueryRuntimeError(cudnn_handle(), &status2, CUDNN_ERRQUERY_RAWCODE, NULL);
#endif
	if (status != CUDNN_STATUS_SUCCESS)
	{
		darknet_fatal_error(filename, function, line, "cuDNN current error: status=%d, %s", status, cudnnGetErrorString(status));
	}

	if (status2 != CUDNN_STATUS_SUCCESS)
	{
		darknet_fatal_error(filename, function, line, "cuDNN most recent error: status=%d, %s", status2, cudnnGetErrorString(status2));
	}
}

void cudnn_check_error_extended(cudnnStatus_t status, const char * const filename, const char * const function, const int line)
{
	TAT(TATPARMS);

	if (status != CUDNN_STATUS_SUCCESS)
	{
		fflush(NULL);
		printf("\ncuDNN status error in %s, %s(), line #%d\n", filename, function, line);
		cudnn_check_error(status, filename, function, line);
	}
#if defined(DEBUG) || defined(CUDA_DEBUG)
	cuda_debug_sync = 1;
#endif
	if (cuda_debug_sync)
	{
		cudaError_t cuda_status = cudaDeviceSynchronize();
		if (cuda_status != (cudaError_t)CUDA_SUCCESS)
		{
			printf("\ncudaDeviceSynchronize() error in %s, %s(), line #%d\n", filename, function, line);
		}
	}
	cudnn_check_error(status, filename, function, line);
}

static cudnnHandle_t switchCudnnHandle[16];
static int switchCudnnInit[16];
#endif


void cublas_check_error(cublasStatus_t status)
{
	TAT(TATPARMS);

#if defined(DEBUG) || defined(CUDA_DEBUG)
	cudaDeviceSynchronize();
#endif
	if (cuda_debug_sync)
	{
		cudaDeviceSynchronize();
	}
	if (status != CUBLAS_STATUS_SUCCESS) {
		printf("cuBLAS Error\n");
	}
}

void cublas_check_error_extended(cublasStatus_t status, const char * const filename, const char * const function, const int line)
{
	TAT(TATPARMS);

	if (status != CUBLAS_STATUS_SUCCESS)
	{
	printf("\n cuBLAS status Error in: file: %s function: %s() line: %d\n", filename, function, line);
	}
#if defined(DEBUG) || defined(CUDA_DEBUG)
	cuda_debug_sync = 1;
#endif
	if (cuda_debug_sync)
	{
		cudaError_t cuda_status = cudaDeviceSynchronize();
		if (cuda_status != (cudaError_t)CUDA_SUCCESS)
		{
			printf("\ncudaDeviceSynchronize() Error in: file: %s function: %s() line: %d\n", filename, function, line);
		}
	}
	cublas_check_error(status);
}

static int blasInit[16] = { 0 };
static cublasHandle_t blasHandle[16];

cublasHandle_t blas_handle()
{
	TAT(TATPARMS);

	int i = cuda_get_device();
	if (!blasInit[i]) {
		CHECK_CUBLAS(cublasCreate(&blasHandle[i]));
		cublasStatus_t status = cublasSetStream(blasHandle[i], get_cuda_stream());
		CHECK_CUBLAS(status);
		blasInit[i] = 1;
	}
	return blasHandle[i];
}


//static int switchBlasInit[16] = { 0 };
//static cublasHandle_t switchBlasHandle[16];

static cudaStream_t switchStreamsArray[16];
static int switchStreamInit[16] = { 0 };

cudaStream_t switch_stream(int i)
{
	TAT(TATPARMS);

	int dev_id = cuda_get_device();

	//printf(" switch_stream = %d \n", i);
	if (!switchStreamInit[i]) {
		CHECK_CUDA(cudaStreamCreateWithFlags(&switchStreamsArray[i], cudaStreamNonBlocking));
		switchStreamInit[i] = 1;
		printf(" Create stream %d \n", i);
	}

	//cudaStreamQuery(streamsArray[0]);   // Flush previous stream queue
	streamsArray[dev_id] = switchStreamsArray[i];
	streamInit[dev_id] = switchStreamInit[i];

	//printf("switch_stream %d - get_cuda_stream() = %d \n", i, get_cuda_stream());

	/*
	if (!switchBlasInit[i]) {
		CHECK_CUDA( cublasCreate(&switchBlasHandle[i]) );
		switchBlasInit[i] = 1;
		CHECK_CUDA( cublasSetStream(switchBlasHandle[i], switchStreamsArray[i]) );
		printf(" Create blas-handle %d \n", i);
	}
	blasHandle[dev_id] = switchBlasHandle[i];
	blasInit[dev_id] = switchBlasInit[i];
	*/

#ifdef CUDNN
	if (!switchCudnnInit[i]) {
		CHECK_CUDNN( cudnnCreate(&switchCudnnHandle[i]) );
		switchCudnnInit[i] = 1;
		CHECK_CUDNN(cudnnSetStream(switchCudnnHandle[i], switchStreamsArray[i]));
		printf(" Create cudnn-handle %d \n", i);
	}
	cudnnHandle[dev_id] = switchCudnnHandle[i];
	cudnnInit[dev_id] = switchCudnnInit[i];
#endif

	return switchStreamsArray[i];
}

#ifndef cudaEventWaitDefault
#define cudaEventWaitDefault 0x00
#endif // cudaEventWaitDefault

static const int max_events = 1024;
static cudaEvent_t switchEventsArray[1024];
static volatile int event_counter = 0;

void wait_stream(int i)
{
	TAT(TATPARMS);

	int dev_id = cuda_get_device();
	if (event_counter >= max_events)
	{
		darknet_fatal_error(DARKNET_LOC, "CUDA max_events exceeded");
	}

	CHECK_CUDA( cudaEventCreateWithFlags(&switchEventsArray[event_counter], cudaEventDisableTiming) );
	//printf(" create event = %d (wait for stream = %d) \n", event_counter, i);

	//CHECK_CUDA(cudaEventRecordWithFlags(switchEventsArray[i], switchStreamsArray[i], cudaEventRecordExternal) );
	CHECK_CUDA( cudaEventRecord(switchEventsArray[event_counter], switchStreamsArray[i]) );
	CHECK_CUDA( cudaStreamWaitEvent(streamsArray[dev_id], switchEventsArray[event_counter], cudaEventWaitDefault) );
	//cudaStreamWaitEvent(streamsArray[dev_id], switchEventsArray[i], cudaEventWaitExternal);
	event_counter++;
}

void reset_wait_stream_events() {
	int i;
	for (i = 0; i < event_counter; ++i) {
		CHECK_CUDA(cudaEventDestroy(switchEventsArray[i]));
	}
	event_counter = 0;
}


namespace
{
	static float **pinned_ptr = NULL;
	static size_t pinned_num_of_blocks = 0;
	static size_t pinned_index = 0;
	static size_t pinned_block_id = 0;
	static const size_t pinned_block_size = (size_t)1024 * 1024 * 1024 * 1;   // 1 GB block size

	static std::mutex mutex_pinned;
}


// free CPU-pinned memory
void free_pinned_memory()
{
	TAT(TATPARMS);

	if (pinned_ptr) {
		int k;
		for (k = 0; k < pinned_num_of_blocks; ++k) {
			cuda_free_host(pinned_ptr[k]);
		}
		free(pinned_ptr);
		pinned_ptr = NULL;
	}
}

// custom CPU-pinned memory allocation
void pre_allocate_pinned_memory(const size_t size)
{
	TAT(TATPARMS);

	const size_t num_of_blocks = size / pinned_block_size + ((size % pinned_block_size) ? 1 : 0);

	std::cout << "pre_allocate: pinned_ptr = " << pinned_ptr << std::endl;

	std::scoped_lock lock(mutex_pinned);

	if (!pinned_ptr)
	{
		pinned_ptr = (float **)calloc(num_of_blocks, sizeof(float *));
		if(!pinned_ptr)
		{
			darknet_fatal_error(DARKNET_LOC, "calloc failed with num_of_blocks=%d", num_of_blocks);
		}

		std::cout
			<< "pre_allocate:"
			<< " size="				<< size_to_IEC_string(size)
			<< ", num_of_blocks="	<< num_of_blocks
			<< ", block_size="		<< size_to_IEC_string(pinned_block_size)
			<< std::endl;

		for (int k = 0; k < num_of_blocks; ++k)
		{
			cudaError_t status = cudaHostAlloc((void **)&pinned_ptr[k], pinned_block_size, cudaHostRegisterMapped);
			if (status != cudaSuccess)
			{
				Darknet::display_warning_msg("cannot pre-allocate CUDA-pinned buffer on CPU-RAM\n");
			}
			CHECK_CUDA(status);
			if (!pinned_ptr[k])
			{
				darknet_fatal_error(DARKNET_LOC, "cudaHostAlloc() failed, k=%d, num=%ul, size=%ul", k, num_of_blocks, pinned_block_size);
			}

			std::cout << (k + 1) << "/" << num_of_blocks << ": allocated " << size_to_IEC_string(pinned_block_size) << " pinned block" << std::endl;
		}
		pinned_num_of_blocks = num_of_blocks;
	}
}

// simple - get pre-allocated pinned memory
float *cuda_make_array_pinned_preallocated(float *x, size_t n)
{
	TAT(TATPARMS);

	std::scoped_lock lock(mutex_pinned);

	float *x_cpu = NULL;
	const size_t memory_step = 512;// 4096;
	const size_t size = sizeof(float)*n;
	const size_t allocation_size = ((size / memory_step) + 1) * memory_step;

	if (pinned_ptr && pinned_block_id < pinned_num_of_blocks && (allocation_size < pinned_block_size/2))
	{
		if ((allocation_size + pinned_index) > pinned_block_size)
		{
			const float filled = 100.0f * pinned_index / pinned_block_size;
			std::cout << "Pinned block_id=" << pinned_block_id << ", filled=" << filled << std::endl;
			pinned_block_id++;
			pinned_index = 0;
		}
		if ((allocation_size + pinned_index) < pinned_block_size && pinned_block_id < pinned_num_of_blocks)
		{
			x_cpu = (float *)((char *)pinned_ptr[pinned_block_id] + pinned_index);
			pinned_index += allocation_size;
		}
		else
		{
			//printf("Pre-allocated pinned memory is over! \n");
		}
	}

	if(!x_cpu)
	{
		if (allocation_size > pinned_block_size / 2)
		{
			std::cout << "Try to allocate new pinned memory, size=" << size_to_IEC_string(size) << std::endl;
			cudaError_t status = cudaHostAlloc((void **)&x_cpu, size, cudaHostRegisterMapped);
			if (status != cudaSuccess)
			{
				Darknet::display_warning_msg("Cannot allocate CUDA-pinned memory on CPU-RAM (pre-allocated memory is over too)\n");
			}
			CHECK_CUDA(status);
		}
		else
		{
			std::cout << "Try to allocate new pinned BLOCK, size=" << size_to_IEC_string(size) << std::endl;
			pinned_num_of_blocks++;
			pinned_block_id = pinned_num_of_blocks - 1;
			pinned_index = 0;
			pinned_ptr = (float **)realloc(pinned_ptr, pinned_num_of_blocks * sizeof(float *));
			cudaError_t status = cudaHostAlloc((void **)&pinned_ptr[pinned_block_id], pinned_block_size, cudaHostRegisterMapped);
			if (status != cudaSuccess)
			{
				Darknet::display_warning_msg("Cannot pre-allocate CUDA-pinned buffer on CPU-RAM\n");
			}
			CHECK_CUDA(status);
			x_cpu = pinned_ptr[pinned_block_id];
		}
	}

	if (x)
	{
		cudaError_t status = cudaMemcpyAsync(x_cpu, x, size, cudaMemcpyDefault, get_cuda_stream());
		CHECK_CUDA(status);
	}

	return x_cpu;
}

float *cuda_make_array_pinned(float *x, size_t n)
{
	TAT(TATPARMS);

	float *x_gpu;
	size_t size = sizeof(float)*n;
	//cudaError_t status = cudaMalloc((void **)&x_gpu, size);
	cudaError_t status = cudaHostAlloc((void **)&x_gpu, size, cudaHostRegisterMapped);
	if (status != cudaSuccess) fprintf(stderr, " Can't allocate CUDA-pinned memory on CPU-RAM \n");
	CHECK_CUDA(status);
	if (x) {
		status = cudaMemcpyAsync(x_gpu, x, size, cudaMemcpyDefault, get_cuda_stream());
		CHECK_CUDA(status);
	}
	if (!x_gpu)
	{
		darknet_fatal_error(DARKNET_LOC, "cudaHostAlloc failed");
	}

	return x_gpu;
}

float *cuda_make_array(float *x, size_t n)
{
	TAT(TATPARMS);

	const size_t size = n * sizeof(float);
//    printf("allocating CUDA memory: %d floats (%s)\n", n, size_to_IEC_string(size));

	float * x_gpu = NULL;
	cudaError_t status = cudaMalloc((void **)&x_gpu, size);
	if (status != cudaSuccess || !x_gpu)
	{
		darknet_fatal_error(DARKNET_LOC, "CUDA memory allocation failed (%s).\nIf possible, try to set subdivisions=... higher in your cfg file.", size_to_IEC_string(size));
	}
	CHECK_CUDA(status);

	if (x)
	{
//        status = cudaMemcpy(x_gpu, x, size, cudaMemcpyHostToDevice);
		status = cudaMemcpyAsync(x_gpu, x, size, cudaMemcpyDefault, get_cuda_stream());
		CHECK_CUDA(status);
	}

	return x_gpu;
}

void **cuda_make_array_pointers(void **x, size_t n)
{
	TAT(TATPARMS);

	void **x_gpu;
	size_t size = sizeof(void*) * n;
	cudaError_t status = cudaMalloc((void **)&x_gpu, size);
	if (status != cudaSuccess) fprintf(stderr, "Try increasing subdivisions=... in your cfg file.\n");
	CHECK_CUDA(status);
	if (x) {
		status = cudaMemcpyAsync(x_gpu, x, size, cudaMemcpyDefault, get_cuda_stream());
		CHECK_CUDA(status);
	}
	if (!x_gpu)
	{
		darknet_fatal_error(DARKNET_LOC, "CUDA malloc failed (%s)", size_to_IEC_string(size));
	}
	return x_gpu;
}

void cuda_random(float *x_gpu, size_t n)
{
	TAT(TATPARMS);

	static curandGenerator_t gen[16];
	static int init[16] = {0};
	int i = cuda_get_device();
	if(!init[i]){
		curandCreateGenerator(&gen[i], CURAND_RNG_PSEUDO_DEFAULT);
		curandSetPseudoRandomGeneratorSeed(gen[i], time(0));
		init[i] = 1;
	}
	curandGenerateUniform(gen[i], x_gpu, n);
	CHECK_CUDA(cudaPeekAtLastError());
}

float cuda_compare(float *x_gpu, float *x, size_t n, char *s)
{
	TAT(TATPARMS);

	float* tmp = (float*)xcalloc(n, sizeof(float));
	cuda_pull_array(x_gpu, tmp, n);
	//int i;
	//for(i = 0; i < n; ++i) printf("%f %f\n", tmp[i], x[i]);
	axpy_cpu(n, -1, x, 1, tmp, 1);
	float err = dot_cpu(n, tmp, 1, tmp, 1);
	printf("Error %s: %f\n", s, sqrt(err/n));
	free(tmp);
	return err;
}

int *cuda_make_int_array(size_t n)
{
	TAT(TATPARMS);

	int *x_gpu;
	size_t size = sizeof(int)*n;
	cudaError_t status = cudaMalloc((void **)&x_gpu, size);
	if(status != cudaSuccess) fprintf(stderr, " Try to set subdivisions=... to a larger value in your cfg-file. \n");
	CHECK_CUDA(status);
	return x_gpu;
}

int *cuda_make_int_array_new_api(int *x, size_t n)
{
	TAT(TATPARMS);

	int *x_gpu;
	size_t size = sizeof(int)*n;
	cudaError_t status = cudaMalloc((void **)&x_gpu, size);
	CHECK_CUDA(status);
	if (x) {
		//status = cudaMemcpy(x_gpu, x, size, cudaMemcpyHostToDevice);
		status = cudaMemcpyAsync(x_gpu, x, size, cudaMemcpyHostToDevice, get_cuda_stream());
		CHECK_CUDA(status);
	}
	if (!x_gpu)
	{
		darknet_fatal_error(DARKNET_LOC, "CUDA malloc failed (%s)", size_to_IEC_string(size));
	}
	return x_gpu;
}

void cuda_free(float *x_gpu)
{
	TAT(TATPARMS);

	//cudaStreamSynchronize(get_cuda_stream());
	cudaError_t status = cudaFree(x_gpu);
	CHECK_CUDA(status);
}

void cuda_free_host(float *x_cpu)
{
	TAT(TATPARMS);

	//cudaStreamSynchronize(get_cuda_stream());
	cudaError_t status = cudaFreeHost(x_cpu);
	CHECK_CUDA(status);
}

void cuda_push_array(float *x_gpu, float *x, size_t n)
{
	TAT(TATPARMS);

	const size_t size = n * sizeof(float);
//    printf("cuda_push_array(): src=%p, dst=%p, n=%d, total_size=%d (%s)\n", x, x_gpu, n, size, size_to_IEC_string(size));
//    cudaError_t status = cudaMemcpy(x_gpu, x, size, cudaMemcpyHostToDevice);
	cudaError_t status = cudaMemcpyAsync(x_gpu, x, size, cudaMemcpyHostToDevice, get_cuda_stream());
	CHECK_CUDA(status);
}

void cuda_pull_array(float *x_gpu, float *x, size_t n)
{
	TAT(TATPARMS);

	size_t size = sizeof(float)*n;
	//cudaError_t status = cudaMemcpy(x, x_gpu, size, cudaMemcpyDeviceToHost);
	//printf("cuda_pull_array - get_cuda_stream() = %d \n", get_cuda_stream());
	cudaError_t status = cudaMemcpyAsync(x, x_gpu, size, cudaMemcpyDeviceToHost, get_cuda_stream());
	CHECK_CUDA(status);
	cudaStreamSynchronize(get_cuda_stream());
}

void cuda_pull_array_async(float *x_gpu, float *x, size_t n)
{
	TAT(TATPARMS);

	size_t size = sizeof(float)*n;
	cudaError_t status = cudaMemcpyAsync(x, x_gpu, size, cudaMemcpyDefault, get_cuda_stream());
	check_cuda_error(status, DARKNET_LOC);
	//cudaStreamSynchronize(get_cuda_stream());
}

int get_number_of_blocks(int array_size, int block_size)
{
	return array_size / block_size + ((array_size % block_size > 0) ? 1 : 0);
}

int get_gpu_compute_capability(int i, char *device_name)
{
	typedef struct cudaDeviceProp cudaDeviceProp;
	cudaDeviceProp prop;
	cudaError_t status = cudaGetDeviceProperties(&prop, i);
	CHECK_CUDA(status);
	if (device_name) strcpy(device_name, prop.name);
	int cc = prop.major * 100 + prop.minor * 10;    // __CUDA_ARCH__ format
	return cc;
}

void show_cuda_cudnn_info()
{
	int device_count			= 0;
	int cuda_runtime_version	= 0;
	int cuda_driver_version		= 0;

	CHECK_CUDA(cudaGetDeviceCount	(&device_count			));
	CHECK_CUDA(cudaRuntimeGetVersion(&cuda_runtime_version	));
	CHECK_CUDA(cudaDriverGetVersion	(&cuda_driver_version	));

	int cuda_runtime_major	= cuda_runtime_version / 1000;
	int cuda_runtime_minor	= (cuda_runtime_version - cuda_runtime_major * 1000) / 10;
	int cuda_driver_major	= cuda_driver_version / 1000;
	int cuda_driver_minor	= (cuda_driver_version - cuda_driver_major * 1000) / 10;

	std::cout
		<< "CUDA runtime version " << cuda_runtime_version
		<< " ("
		<< Darknet::in_colour(Darknet::EColour::kBrightWhite) << "v" << cuda_runtime_major << "." << cuda_runtime_minor
		<< Darknet::in_colour(Darknet::EColour::kNormal)
		<< "), "
		<< "driver version " << cuda_driver_version
		<< " ("
		<< Darknet::in_colour(Darknet::EColour::kBrightWhite) << "v" << cuda_driver_major << "." << cuda_driver_minor
		<< Darknet::in_colour(Darknet::EColour::kNormal)
		<< ")"
		<< std::endl;

#ifndef CUDNN
	std::cout << "cuDNN is DISABLED" << std::endl;
#else
	size_t cudnn_version = cudnnGetCudartVersion();
	int cudnn_version_major = 0;
	int cudnn_version_minor = 0;
	int cudnn_version_patch = 0;
	cudnnGetProperty(MAJOR_VERSION, &cudnn_version_major);
	cudnnGetProperty(MINOR_VERSION, &cudnn_version_minor);
	cudnnGetProperty(PATCH_LEVEL, &cudnn_version_patch);
	std::cout
		<< "cuDNN version " << cudnn_version
		<< " ("
		<< Darknet::in_colour(Darknet::EColour::kBrightWhite)
		<< "v" << cudnn_version_major
		<< "." << cudnn_version_minor
		<< "." << cudnn_version_patch
		<< Darknet::in_colour(Darknet::EColour::kNormal)
		<< "), "
		<< "use of half-size floats is ";
#ifndef CUDNN_HALF
	std::cout << Darknet::in_colour(Darknet::EColour::kBrightRed, "DISABLED") << std::endl;
#else
	std::cout << Darknet::in_colour(Darknet::EColour::kBrightWhite, "ENABLED") << std::endl;
#endif
#endif

	if (device_count < 1)
	{
		std::cout
			<< "=> "
			<< Darknet::in_colour(Darknet::EColour::kBrightRed, "no CUDA devices")
			<< " (count=" << device_count << ")"
			<< std::endl;
	}
	else
	{
		for (int idx = 0; idx < device_count; idx ++)
		{
			cudaDeviceProp prop;
			cudaGetDeviceProperties(&prop, idx);
			std::cout
				<< "=> " << idx << ": " << Darknet::in_colour(Darknet::EColour::kBrightGreen, prop.name)
				<< " [#" << prop.major << "." << prop.minor << "]"
				<< ", " << Darknet::in_colour(Darknet::EColour::kYellow, size_to_IEC_string(prop.totalGlobalMem))
				<< std::endl;
		}
	}
}

#else // GPU

// When doing a CPU-only build, make this a no-op.
void cuda_set_device(int n)
{
	return;
}

#endif // GPU
