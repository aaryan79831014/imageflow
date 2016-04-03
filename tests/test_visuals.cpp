#include "catch.hpp"
#include "helpers_visual.h"
//#define FLOW_STORE_CHECKSUMS

#ifdef FLOW_STORE_CHECKSUMS
bool store_checksums = true;
#else
bool store_checksums = false;
#endif

TEST_CASE("Test fill_rect", "")
{
    flow_c * c = flow_context_create();
    struct flow_graph * g = flow_graph_create(c, 10, 10, 200, 2.0);
    ERR(c);
    struct flow_bitmap_bgra * b;
    int32_t last;

    last = flow_node_create_canvas(c, &g, -1, flow_bgra32, 400, 300, 0xFFFFFFFF);
    last = flow_node_create_fill_rect(c, &g, last, 0, 0, 50, 100, 0xFF0000FF);
    last = flow_node_create_bitmap_bgra_reference(c, &g, last, &b);
    struct flow_job * job = flow_job_create(c);
    ERR(c);
    if (!flow_job_execute(c, job, &g)) {
        ERR(c);
    }

    REQUIRE(visual_compare(c, b, "FillRect", store_checksums, __FILE__, __func__, __LINE__) == true);
    ERR(c);
    flow_context_destroy(c);
}

TEST_CASE("Test scale image", "")
{

    flow_c * c = flow_context_create();
    size_t bytes_count = 0;
    uint8_t * bytes = get_bytes_cached(c, &bytes_count, "http://www.rollthepotato.net/~john/kevill/test_800x600.jpg");
    REQUIRE(djb2_buffer(bytes, bytes_count) == 0x8ff8ec7a8539a2d5); // Test the checksum. I/O can be flaky

    struct flow_job * job = flow_job_create(c);
    ERR(c);
    int32_t input_placeholder = 0;
    struct flow_io * input = flow_io_create_from_memory(c, flow_io_mode_read_seekable, bytes, bytes_count, job, NULL);
    flow_job_add_io(c, job, input, input_placeholder, FLOW_INPUT);

    struct flow_graph * g = flow_graph_create(c, 10, 10, 200, 2.0);
    ERR(c);
    struct flow_bitmap_bgra * b;
    int32_t last;

    last = flow_node_create_decoder(c, &g, -1, input_placeholder);
    // flow_node_set_decoder_downscale_hint(c, g, last, 400,300,400,300);
    last = flow_node_create_scale(c, &g, last, 400, 300, (flow_interpolation_filter_Robidoux),
                                  (flow_interpolation_filter_Robidoux));
    last = flow_node_create_bitmap_bgra_reference(c, &g, last, &b);
    ERR(c);
    if (!flow_job_execute(c, job, &g)) {
        ERR(c);
    }

    REQUIRE(visual_compare(c, b, "ScaleThePotato", store_checksums, __FILE__, __func__, __LINE__) == true);
    ERR(c);
    flow_context_destroy(c);
}

extern flow_interpolation_filter jpeg_block_filter;
extern float jpeg_sharpen_percent_goal;
extern float jpeg_block_filter_blur;
bool get_image_dimensions(flow_c *c, uint8_t * bytes, size_t bytes_count, int32_t * width, int32_t * height){
    struct flow_job * job = flow_job_create(c);

    struct flow_io * input = flow_io_create_from_memory(c, flow_io_mode_read_seekable, bytes, bytes_count, job, NULL);
    flow_job_add_io(c, job, input, 0, FLOW_INPUT);
    struct flow_decoder_info info;
    flow_job_get_decoder_info(c, job, 0, &info);
    *width = info.frame0_width;
    *height = info.frame0_height;
    flow_job_destroy(c, job);
    return true;
}

bool scale_down(flow_c * c, uint8_t * bytes, size_t bytes_count, int block_scale_to_x,
                int block_scale_to_y, int scale_to_x, int scale_to_y, flow_interpolation_filter precise_filter, flow_interpolation_filter block_filter,float post_sharpen, float blur, flow_bitmap_bgra ** ref)
{
    struct flow_job * job = flow_job_create(c);

    int32_t input_placeholder = 0;
    struct flow_io * input = flow_io_create_from_memory(c, flow_io_mode_read_seekable, bytes, bytes_count, job, NULL);
    if (input == NULL){
        FLOW_add_to_callstack(c);
        return false;
    }
    if (!flow_job_add_io(c, job, input, input_placeholder, FLOW_INPUT)){
        FLOW_add_to_callstack(c);
        return false;
    }

    struct flow_graph * g = flow_graph_create(c, 10, 10, 200, 2.0);
    if (g == NULL){
        FLOW_add_to_callstack(c);
        return false;
    }
    struct flow_bitmap_bgra * b;
    int32_t last;

    last = flow_node_create_decoder(c, &g, -1, input_placeholder);

    if (block_scale_to_x > 0) {
        if (!flow_job_decoder_set_downscale_hints_by_placeholder_id(
                c, job, input_placeholder, block_scale_to_x, block_scale_to_y, block_scale_to_x, block_scale_to_y)) {
            FLOW_add_to_callstack(c);
            return false;
        }
    }
    struct flow_decoder_info info;
    if (!flow_job_get_decoder_info(c, job, input_placeholder, &info)) {
        FLOW_add_to_callstack(c);
        return false;
    }
    last = flow_node_create_primitive_crop(c, &g, last, 0, 0, info.frame0_width, info.frame0_height);
    if (scale_to_x != block_scale_to_x || scale_to_y != block_scale_to_y) {
        last = flow_node_create_scale(c, &g, last, scale_to_x, scale_to_y, precise_filter,precise_filter);
    }
    last = flow_node_create_bitmap_bgra_reference(c, &g, last, ref);
    //SET GLOBAL VARS
    //TODO: kill
    jpeg_block_filter = block_filter;
    jpeg_sharpen_percent_goal = 0;
    jpeg_block_filter_blur = blur;
    if (flow_context_has_error(c)){
        FLOW_add_to_callstack(c);
        return false;
    }
    if (!flow_job_execute(c, job, &g)) {
        FLOW_add_to_callstack(c);
        return false;
    }

    //Let the bitmap last longer than the context or job
    if (!flow_set_owner(c, *ref, NULL)) {
        FLOW_add_to_callstack(c);
        return false;
    }

    if (!flow_job_destroy(c, job)) {
        FLOW_add_to_callstack(c);
        return false;
    }
    return true;
}



TEST_CASE("Test 8->4 downscaling contrib windows",""){
    flow_c * c = flow_context_create();
    struct flow_interpolation_details * details = flow_interpolation_details_create_bicubic_custom(
        c, 2, 1. / 1.1685777620836932, 0.37821575509399867, 0.31089212245300067);

    struct flow_interpolation_line_contributions * contrib = flow_interpolation_line_contributions_create(c, 4, 8, details);


    REQUIRE(contrib->ContribRow[0].Weights[0] == Approx(0.45534f));
    REQUIRE(contrib->ContribRow[3].Weights[contrib->ContribRow[3].Right - contrib->ContribRow[3].Left -1] == Approx(0.45534f));
    flow_context_destroy(c);
}


TEST_CASE("Test 8->1 downscaling contrib windows",""){
    return; //skip
    flow_c * c = flow_context_create();

    int block_filter = 1;
    for (block_filter = 1; block_filter <= flow_interpolation_filter_MitchellFast; block_filter++) {
        struct flow_interpolation_details * details = flow_interpolation_details_create_from(c, (flow_interpolation_filter)block_filter);
        if (details == NULL){
            ERR(c);
        }
        float blurs[] = {0, 1.0, 0.95, 0.85, 0.7};

        for (uint64_t blur = 0; blur < sizeof(blurs) / sizeof(float); blur++) {

            for (float sharpen_percent_goal = 0; sharpen_percent_goal < 101; sharpen_percent_goal += 10) {
                details->sharpen_percent_goal = jpeg_sharpen_percent_goal;

                if (blurs[blur] > 0) details->blur = blurs[blur];

                struct flow_interpolation_line_contributions * contrib = flow_interpolation_line_contributions_create(c,
                                                                                                                      1,
                                                                                                                      8,
                                                                                                                      details);
                float * weights = &contrib->ContribRow[0].Weights[0];
                fprintf(stdout, "filter %d, sharpen %.02f: %.010f %.010f %.010f %.010f %.010f %.010f %.010f %.010f\n",
                        block_filter, sharpen_percent_goal, weights[0], weights[1], weights[2], weights[3], weights[4],
                        weights[5], weights[6], weights[7]);
//            REQUIRE(contrib->ContribRow[0].Weights[0] == Approx(0.45534f));
//            REQUIRE(contrib->ContribRow[0].Weights[contrib->ContribRow[0].Right - contrib->ContribRow[0].Left - 1] ==
//                    Approx(0.45534f));
            }
        }
    }
    flow_context_destroy(c);
}

static const char * const test_images[] = {

    "http://s3-us-west-2.amazonaws.com/imageflow-resources/reference_image_originals/vgl_6548_0026.jpg",
    "http://s3-us-west-2.amazonaws.com/imageflow-resources/reference_image_originals/vgl_6434_0018.jpg",
    "http://s3-us-west-2.amazonaws.com/imageflow-resources/reference_image_originals/vgl_5674_0098.jpg",
    "http://s3.amazonaws.com/resizer-images/u6.jpg",
    "https://s3.amazonaws.com/resizer-images/u1.jpg",
    "http://s3-us-west-2.amazonaws.com/imageflow-resources/reference_image_originals/artificial.jpg",
    "http://www.rollthepotato.net/~john/kevill/test_800x600.jpg",
        "http://s3-us-west-2.amazonaws.com/imageflow-resources/reference_image_originals/nightshot_iso_100.jpg",
};
static const char * const test_image_names[] = {

    "vgl_6548_0026.jpg",
    "vgl_6434_0018.jpg",
    "vgl_5674_0098.jpg",
    "u6.jpg (from unsplash)",
    "u1.jpg (from unsplash)",
    "artificial.jpg",
    "kevill/test_800x600.jpg",
    "nightshot_iso_100.jpg",
};
static const  unsigned long test_image_checksums[] = {

    12408886241370335986UL,
    4555980965349232399UL,
    16859055904024046582UL,
    4586057909633522523UL,
    4732395045697209035UL,
    0x4bc30144f62925c1,
    0x8ff8ec7a8539a2d5,
    6083832193877068235L,

};
#define TEST_IMAGE_COUNT (sizeof(test_image_checksums) / sizeof(unsigned long))
// We are using an 'ideal' scaling of the full image as a control
// Under srgb decoding (libjpeg-turbo as-is ISLOW downsampling), DSSIM=0.003160
// Under linear light decoder box downsampling (vs linear light true resampling), DSSIM=0.002947
// Using the flow_bitmap_float scaling in two dierctions, DSSIM=0.000678

//Compared against robidoux
//All filters are equal for 1/8th 0.00632
//Lowest DSSIM for 2/8 was 0.00150 for block interpolation filter 2
//Lowest DSSIM for 2/8 was 0.001493900 for block interpolation filter 14, sharpen 0.000000, blur 0.850000
//Lowest DSSIM for 3/8 was 0.00078 for block interpolation filter 14
//Lowest DSSIM for 4/8 was 0.00065 for block interpolation filter 3
//Lowest DSSIM for 5/8 was 0.00027 for block interpolation filter 2
//Lowest DSSIM for 6/8 was 0.00018 for block interpolation filter 2
//Lowest DSSIM for 7/8 was 0.00007 for block interpolation filter 2
//
//image used                    , 1/8 - DSSIM, 1/8 - Params,  2/8 - DSSIM, 2/8 - Params, 3/8 - DSSIM, 3/8 - Params, 4/8 - DSSIM, 4/8 - Params, 5/8 - DSSIM, 5/8 - Params, 6/8 - DSSIM, 6/8 - Params, 7/8 - DSSIM, 7/8 - Params,
//u6.jpg (from unsplash)        , 0.0046863400, f2 b0.00 s0.00, 0.0029699400, f3 b0.85 s0.00, 0.0040962400, f2 b0.90 s0.00, 0.0004396400, f2 b0.00 s0.00, 0.0006252100, f2 b0.00 s0.00, 0.0016532400, f2 b0.00 s0.00, 0.0031835700, f2 b0.90 s0.00,
//u1.jpg (from unsplash)        , 0.0114932700, f2 b0.00 s0.00, 0.0018476900, f2 b0.85 s0.00, 0.0031388100, f2 b0.00 s0.00, 0.0006262900, f2 b0.85 s0.00, 0.0021540200, f2 b0.00 s0.00, 0.0002931700, f2 b0.85 s0.00, 0.0017235700, f2 b0.00 s0.00,
//artificial.jpg                , 0.0023080300, f2 b0.00 s0.00, 0.0003323700, f3 b0.85 s0.00, 0.0001204200, f2 b0.85 s0.00, 0.0001255500, f14 b0.85 s0.00, 0.0000715400, f2 b0.00 s0.00, 0.0000685200, f2 b0.85 s0.00, 0.0000570500, f2 b0.85 s0.00,
//kevill/test_800x600.jpg       , 0.0063219000, f2 b0.00 s0.00, 0.0014939000, f14 b0.85 s0.00, 0.0007776700, f14 b0.00 s0.00, 0.0006515500, f3 b0.90 s0.00, 0.0002729900, f2 b0.85 s0.00, 0.0001801800, f2 b0.00 s0.00, 0.0000702500, f2 b0.00 s0.00,
//nightshot_iso_100.jpg         , 0.0013806700, f2 b0.00 s0.00, 0.0003930900, f3 b0.80 s0.00, 0.0002217200, f14 b0.85 s0.00, 0.0002291900, f2 b0.80 s0.00, 0.0002127800, f2 b0.85 s0.00, 0.0002054500, f2 b0.80 s0.00, 0.0001715300, f14 b0.80 s0.00,
//vgl_6548_0026.jpg             , 0.0396496800, f2 b0.00 s0.00, 0.0014971800, f2 b0.90 s0.00, 0.0009725300, f2 b0.85 s0.00, 0.0007197700, f2 b0.90 s0.00, 0.0006977400, f2 b0.00 s0.00, 0.0004041700, f2 b0.00 s0.00, 0.0001562700, f14 b0.90 s0.00,
//vgl_6434_0018.jpg             , 0.0045688400, f2 b0.00 s0.00, 0.0004874900, f3 b0.85 s0.00, 0.0002206900, f2 b0.85 s0.00, 0.0002524000, f14 b0.85 s0.00, 0.0002299500, f2 b0.85 s0.00, 0.0002411100, f2 b0.85 s0.00, 0.0002378800, f2 b0.00 s0.00,
//vgl_5674_0098.jpg             , 0.0258202300, f2 b0.00 s0.00, 0.0017091900, f2 b0.00 s0.00, 0.0009284800, f14 b0.90 s0.00, 0.0008620000, f14 b0.00 s0.00, 0.0005750100, f2 b0.00 s0.00, 0.0004875900, f14 b0.00 s0.00, 0.0003509500, f2 b0.00 s0.00,

//For 5/8 and 6/8 and 7/8 - stick to f2, but search space between blur 0.85 and 1
//For 2/8, search f2 and f3 between the 0.80 space and the 1.0 space
//For 1/8 use 2, but try various levels of post-sharpening
//For 3/8 and 4/8, stick with f2 repeat search space with full logging
struct downscale_test_result{
    double best_dssim[7];
    flow_interpolation_filter best_filter[7];
    float best_sharpen[7];
    float best_blur[7];
};

void print_results(struct downscale_test_result * results, const char* const* image_names, size_t result_count){
    fprintf(stdout, "%-30s, 1/8 - DSSIM, 1/8 - Params,  2/8 - DSSIM, 2/8 - Params, 3/8 - DSSIM, 3/8 - Params, 4/8 - DSSIM, 4/8 - Params, 5/8 - DSSIM, 5/8 - Params, 6/8 - DSSIM, 6/8 - Params, 7/8 - DSSIM, 7/8 - Params,\n", "image used");
    for (size_t ix = 0; ix < result_count; ix++){
        fprintf(stdout, "%-30s, ", image_names[ix]);
        for (int scale = 0; scale < 7; scale++) {
            fprintf(stdout, "%.010f, f%d b%0.2f s%0.2f, ", results[ix].best_dssim[scale], results[ix].best_filter[scale],
                    results[ix].best_blur[scale], results[ix].best_sharpen[scale]);
        }
        fprintf(stdout, "\n");
    }
}
TEST_CASE("Test downscale image during decoding", "")
{
    flow_interpolation_filter filters[] = { flow_interpolation_filter_Robidoux};
    float blurs[] = {0.75, 0.8, 0.83, 1. / 1.1685777620836932, 0.87, 0.9, 0.93, 0.97, 1.0};


    struct downscale_test_result results[TEST_IMAGE_COUNT];
    memset(&results[0], 0, sizeof(results));
    for (size_t test_image_index =0; test_image_index < TEST_IMAGE_COUNT; test_image_index++) {

        fprintf(stdout, "Testing with %s\n\n", test_images[test_image_index]);
        flow_c * c = flow_context_create();
        size_t bytes_count = 0;
        uint8_t * bytes = get_bytes_cached(c, &bytes_count,
                                           test_images[test_image_index]);
        unsigned long input_checksum = djb2_buffer(bytes, bytes_count);
        REQUIRE(input_checksum == test_image_checksums[test_image_index]); // Test the checksum. I/O can be flaky
        int original_width, original_height;
        REQUIRE(get_image_dimensions(c, bytes, bytes_count, &original_width, &original_height) == true);


        for (int scale_to = 1; scale_to > 0; scale_to--) {
            long new_w = (original_width * scale_to + 8 - 1L) / 8L;
            long new_h = (original_height * scale_to + 8 - 1L) / 8L;
            fprintf(stdout, "Testing downscaling to %d/8: %dx%d -> %ldx%ld\n", scale_to, original_width,
                    original_height, new_w, new_h);

            double best_dssim = 1;

            size_t block_filter = 1;
            for (block_filter = 0; block_filter < sizeof(filters) / sizeof(flow_interpolation_filter); block_filter++) {

                for (uint64_t blur = 0; blur < sizeof(blurs) / sizeof(float); blur++) {

                    for (float post_sharpen = 0; post_sharpen < 1; post_sharpen += 50) {
                        flow_c * inner_context = flow_context_create();
                        struct flow_bitmap_bgra * bitmap_a;
                        struct flow_bitmap_bgra * bitmap_b;
                        fprintf(stdout, "filter %i - sharpen_goal %.02f - blur %0.5f: ", (int)filters[block_filter],
                                post_sharpen, blurs[blur]);
                        if (!scale_down(inner_context, bytes, bytes_count, 0, 0, new_w, new_h,
                                        flow_interpolation_filter_Robidoux,
                                        filters[block_filter], 0, blurs[blur],
                                        &bitmap_b)) {
                            ERR(c);
                        }
                        if (!scale_down(inner_context, bytes, bytes_count, new_w, new_h, new_w, new_h,
                                        flow_interpolation_filter_Robidoux, filters[block_filter],
                                        post_sharpen, blurs[blur], &bitmap_a)) {
                            ERR(c);
                        }
                        double dssim;
                        visual_compare_two(inner_context, bitmap_a, bitmap_b,
                                           "Compare ideal downscaling vs downscaling in decoder", &dssim, true, false,
                                           __FILE__,
                                           __func__, __LINE__);
                        if (dssim < best_dssim) {

                            results[test_image_index].best_dssim[scale_to -1] = dssim;
                            best_dssim = dssim;
                            results[test_image_index].best_filter[scale_to -1] = filters[block_filter];
                            results[test_image_index].best_blur [scale_to -1] = blurs[blur];
                            results[test_image_index].best_sharpen[scale_to -1] = post_sharpen;
                        }

                        ERR(inner_context);
                        flow_bitmap_bgra_destroy(inner_context, bitmap_a);
                        flow_bitmap_bgra_destroy(inner_context, bitmap_b);
                        flow_context_destroy(inner_context);
                        inner_context = NULL;
                    }
                }
            }

            print_results(results, test_image_names, test_image_index + 1);
            fflush(stdout);
        }

        flow_context_destroy(c);
    }

    print_results(results, test_image_names, TEST_IMAGE_COUNT);

    fprintf(stdout, "\n\n...done\n");
    sleep(1);
}
