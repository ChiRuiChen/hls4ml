#ifndef NNET_CONV2D_STREAM_H_
#define NNET_CONV2D_STREAM_H_

#include "ap_shift_reg.h"
#include "nnet_common.h"
#include "nnet_conv_stream.h"
#include "hls_stream.h"

namespace nnet {

template<class data_T, typename CONFIG_T>
void compute_scaled_indices_2d(
    const unsigned h_idx,
    const unsigned w_idx,
    ap_uint<CONFIG_T::filt_height * CONFIG_T::filt_width> *pixel_idx
) {
    const unsigned sh_idx = scale_index<CONFIG_T::filt_height, CONFIG_T::stride_height, CONFIG_T::in_height>(h_idx);
    unsigned wp_idx = w_idx * (data_T::size / CONFIG_T::n_chan);

    ComputeIndex: for (unsigned p = 0; p < data_T::size / CONFIG_T::n_chan; p++) {
        #pragma HLS UNROLL

        unsigned sw_idx = scale_index<CONFIG_T::filt_width, CONFIG_T::stride_width, CONFIG_T::in_width>(wp_idx + p);
        pixel_idx[p] = CONFIG_T::pixels[sh_idx * CONFIG_T::min_width + sw_idx];
    }
}

template<class data_T, class res_T, typename CONFIG_T>
void conv_2d_cl2(
    hls::stream<data_T> &data,
    hls::stream<res_T>  &res,
    typename CONFIG_T::weight_t weights[CONFIG_T::filt_height * CONFIG_T::filt_width * CONFIG_T::n_chan * CONFIG_T::n_filt],
    typename CONFIG_T::bias_t   biases[CONFIG_T::n_filt])
{
    assert(CONFIG_T::pad_top == 0 && CONFIG_T::pad_bottom == 0 && CONFIG_T::pad_left == 0 && CONFIG_T::pad_right == 0);
    assert(CONFIG_T::filt_height == CONFIG_T::filt_width);
    assert(CONFIG_T::stride_height <= CONFIG_T::filt_height && CONFIG_T::stride_width <= CONFIG_T::filt_width);

    hls::stream<typename data_T::value_type> data_window[CONFIG_T::filt_height * CONFIG_T::filt_width * CONFIG_T::n_chan];
    const int win_depth = CONFIG_T::filt_height * CONFIG_T::out_width;
    for (unsigned i_out = 0; i_out < CONFIG_T::filt_height * CONFIG_T::filt_width * CONFIG_T::n_chan; i_out++) {
        #pragma HLS STREAM variable=data_window[i_out] depth=win_depth
    }

    #pragma HLS ARRAY_PARTITION variable=CONFIG_T::pixels complete

    res_T res_pack;
    #pragma HLS DATA_PACK variable=res_pack
    unsigned outputs_ready = 0;

    ap_uint<CONFIG_T::filt_height * CONFIG_T::filt_width> pixel_idx[data_T::size / CONFIG_T::n_chan];
    #pragma HLS ARRAY_PARTITION variable=pixel_idx complete

    ReadInputHeight: for (unsigned i_ih = 0; i_ih < CONFIG_T::in_height; i_ih++) {
        ReadInputWidth: for (unsigned i_iw = 0; i_iw < CONFIG_T::in_width / (data_T::size / CONFIG_T::n_chan); i_iw++) {
            #pragma HLS LOOP_FLATTEN
            if (CONFIG_T::strategy == nnet::latency && data_T::size / CONFIG_T::n_chan == 1) {
                #pragma HLS PIPELINE II=CONFIG_T::reuse_factor
            }
            compute_scaled_indices_2d<data_T, CONFIG_T>(i_ih, i_iw, pixel_idx);
            compute_output<data_T, res_T, CONFIG_T>(data.read(), data_window, res, res_pack, outputs_ready, weights, biases, pixel_idx);
        }
    }
}

// Line Buffer
template <class data_T, class res_T, typename CONFIG_T>
void conv_2d_cl(
    hls::stream<data_T> &data,
    hls::stream<res_T>  &res,
    typename CONFIG_T::weight_t weights[CONFIG_T::filt_height * CONFIG_T::filt_width * CONFIG_T::n_chan * CONFIG_T::n_filt],
    typename CONFIG_T::bias_t   biases[CONFIG_T::n_filt])
{
    assert(CONFIG_T::pad_top == 0 && CONFIG_T::pad_bottom == 0 && CONFIG_T::pad_left == 0 && CONFIG_T::pad_right == 0);

    static ap_shift_reg<typename data_T::value_type, CONFIG_T::in_width> line_buffer[CONFIG_T::filt_height - 1][CONFIG_T::n_chan];
    #pragma HLS ARRAY_RESHAPE variable = line_buffer complete dim = 2

    res_T res_pack;
    #pragma HLS DATA_PACK variable=res_pack
    unsigned outputs_ready = 0;

ReadInputHeight: for (int i_ih = 0; i_ih < CONFIG_T::in_height; i_ih++) {
    ReadInputWidth: for (int i_iw = 0; i_iw < CONFIG_T::in_width / (data_T::size / CONFIG_T::n_chan); i_iw++) {
        #pragma HLS LOOP_FLATTEN
            if(CONFIG_T::strategy == nnet::latency && data_T::size / CONFIG_T::n_chan == 1) {
                #pragma HLS PIPELINE II=CONFIG_T::reuse_factor
            }
            compute_output<data_T, res_T, CONFIG_T>(data.read(), line_buffer, res, res_pack, outputs_ready, weights, biases);
        }
    }
}

}
#endif
