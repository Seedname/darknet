/* Darknet/YOLO:  https://github.com/hank-ai/darknet
 * Copyright 2024 Stephane Charette
 */

#include "darknet.hpp"

/** @file
 * This application will display processed images in a GUI window.  Call it like this:
 *
 *     darknet_02_display_annotated_images LegoGears DSCN1580_frame_000034.jpg
 */


int main(int argc, char * argv[])
{
	try
	{
		Darknet::show_version_info();

		Darknet::Parms parms = Darknet::parse_arguments(argc, argv);
		Darknet::NetworkPtr net = Darknet::load_neural_network(parms);
#if 0
		Darknet::set_annotation_font(net, cv::LineTypes::LINE_4, cv::HersheyFonts::FONT_HERSHEY_PLAIN, 2, 2.00);
		Darknet::set_rounded_corner_bounding_boxes(net, true, 1.0f);
#endif

		for (const auto & parm : parms)
		{
			if (parm.type == Darknet::EParmType::kFilename)
			{
				std::cout << "processing " << parm.string << std::endl;

				cv::Mat mat = cv::imread(parm.string);
				cv::imshow("original", mat);

				Darknet::predict_and_annotate(net, mat);
				cv::imshow("annotated", mat);

				const char c = cv::waitKey(-1);
				if (c == 27) // ESC
				{
					break;
				}
			}
		}

		Darknet::free_neural_network(net);
	}
	catch (const std::exception & e)
	{
		std::cout << "Exception: " << e.what() << std::endl;
	}

	return 0;
}
