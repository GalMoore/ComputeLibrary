/*
 * Copyright (c) 2017 ARM Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "arm_compute/core/CL/kernels/CLL2NormalizeLayerKernel.h"

#include "arm_compute/core/CL/CLHelpers.h"
#include "arm_compute/core/CL/CLKernelLibrary.h"
#include "arm_compute/core/CL/ICLTensor.h"
#include "arm_compute/core/FixedPoint.h"
#include "arm_compute/core/Helpers.h"
#include "arm_compute/core/TensorInfo.h"
#include "arm_compute/core/Utils.h"
#include "arm_compute/core/Validate.h"
#include "arm_compute/core/Window.h"

#include "support/ToolchainSupport.h"

using namespace arm_compute;

CLL2NormalizeLayerKernel::CLL2NormalizeLayerKernel()
    : _input(nullptr), _sum(nullptr), _output(nullptr), _axis(0), _epsilon(1e-12)
{
}

void CLL2NormalizeLayerKernel::configure(const ICLTensor *input, const ICLTensor *sum, ICLTensor *output, unsigned int axis, float epsilon)
{
    ARM_COMPUTE_ERROR_ON_DATA_TYPE_CHANNEL_NOT_IN(input, 1, DataType::F32);
    ARM_COMPUTE_ERROR_ON_NULLPTR(output);

    // Sum and output tensor auto initialization if not yet initialized
    auto_init_if_empty(*output->info(), input->info()->tensor_shape(), 1, input->info()->data_type(), input->info()->fixed_point_position());

    ARM_COMPUTE_ERROR_ON_MISMATCHING_DATA_TYPES(input, output);
    ARM_COMPUTE_ERROR_ON_MSG(axis >= TensorShape::num_max_dimensions, "Reduction axis greater than max number of dimensions");
    ARM_COMPUTE_ERROR_ON_MSG(axis > 0, "Unsupported reduction axis, Supported axis is 0");
    ARM_COMPUTE_ERROR_ON_MISMATCHING_SHAPES(input, output);

    _input   = input;
    _sum     = sum;
    _output  = output;
    _axis    = axis;
    _epsilon = epsilon;

    const unsigned int num_elems_processed_per_iteration = 16;

    // Set build options
    std::set<std::string> build_opts;
    build_opts.emplace(("-DDATA_TYPE=" + get_cl_type_from_data_type(input->info()->data_type())));
    build_opts.emplace(("-DVEC_SIZE=" + support::cpp11::to_string(num_elems_processed_per_iteration)));

    // Create kernel
    _kernel = static_cast<cl::Kernel>(CLKernelLibrary::get().create_kernel("l2_normalize", build_opts));

    // Set epsilon argument
    unsigned int idx = num_arguments_per_1D_tensor() * 3;
    _kernel.setArg<cl_uint>(idx, _epsilon);

    // Configure kernel window
    Window win = calculate_max_window(*input->info(), Steps(num_elems_processed_per_iteration));

    AccessWindowHorizontal input_access(input->info(), 0, num_elems_processed_per_iteration);
    AccessWindowHorizontal output_access(output->info(), 0, num_elems_processed_per_iteration);

    update_window_and_padding(win, input_access, output_access);
    output_access.set_valid_region(win, input->info()->valid_region());

    ICLKernel::configure(win);
}

void CLL2NormalizeLayerKernel::run(const Window &window, cl::CommandQueue &queue)
{
    ARM_COMPUTE_ERROR_ON_UNCONFIGURED_KERNEL(this);
    ARM_COMPUTE_ERROR_ON_INVALID_SUBWINDOW(IKernel::window(), window);

    Window window_sum(window);
    window_sum.set(Window::DimX, Window::Dimension(0, 0, 0));

    Window in_slice  = window.first_slice_window_1D();
    Window sum_slice = window_sum.first_slice_window_1D();

    do
    {
        unsigned int idx = 0;
        add_1D_tensor_argument(idx, _input, in_slice);
        add_1D_tensor_argument(idx, _sum, sum_slice);
        add_1D_tensor_argument(idx, _output, in_slice);
        enqueue(queue, *this, in_slice);
    }
    while(window.slide_window_slice_1D(in_slice) && window.slide_window_slice_1D(sum_slice));
}
