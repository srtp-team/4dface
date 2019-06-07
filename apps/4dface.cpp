/*
 * 4dface: Real-time 3D face tracking and reconstruction from 2D video.
 *
 * File: apps/4dface.cpp
 *
 * Copyright 2015, 2016 Patrik Huber
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "helpers.hpp"

#include "eos/core/Landmark.hpp"
#include "eos/core/LandmarkMapper.hpp"
#include "eos/core/Mesh.hpp"
#include "eos/fitting/fitting.hpp"
#include "eos/fitting/contour_correspondence.hpp"
#include "eos/fitting/closest_edge_fitting.hpp"
#include "eos/fitting/RenderingParameters.hpp"
#include "eos/render/utils.hpp"
#include "eos/render/render.hpp"
#include "eos/render/texture_extraction.hpp"
#include "eos/render/draw_utils.hpp"
#include "eos/core/Image_opencv_interop.hpp"

#include "rcr/model.hpp"
#include "cereal/cereal.hpp"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"

#include "Eigen/Dense"

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/objdetect/objdetect.hpp"

#include "boost/program_options.hpp"
#include "boost/filesystem.hpp"

#include <vector>
#include <iostream>
#include <fstream>
using namespace eos;
using namespace std;
namespace po = boost::program_options;
namespace fs = boost::filesystem;
using cv::Mat;
using cv::Vec2f;
using cv::Vec3f;
using cv::Vec4f;
using cv::Rect;
using std::cout;
using std::endl;
using std::vector;
using std::string;

void draw_axes_topright(float r_x, float r_y, float r_z, cv::Mat image);
Mat isomap_former;
/**
 * This app demonstrates facial landmark tracking, estimation of the 3D pose
 * and fitting of the shape model of a 3D Morphable Model from a video stream,
 * and merging of the face texture.
 */
int main(int argc, char *argv[])
{
	int has_movie = 0;
	ofstream Landmark_Pos;
	ofstream shape_File, blendshape_File;
	Landmark_Pos.open("Landmark.txt");
	shape_File.open("shape_File.txt");
	blendshape_File.open("blendshape_File.txt");
	Mat close_isomap,open_isomap, back_ground_img, test_face_img;
	close_isomap = cv::imread("movie_closeeye_isomap.isomap.png",1);
	open_isomap = cv::imread("movie_openeye_isomap.isomap.png", 1);
	back_ground_img = cv::imread("background.png", 1);
	fs::path modelfile, inputvideo, facedetector, landmarkdetector, mappingsfile, contourfile, edgetopologyfile, blendshapesfile;
	try {
		po::options_description desc("Allowed options");
		desc.add_options()
			("help,h",
				"display the help message")
			("morphablemodel,m", po::value<fs::path>(&modelfile)->required()->default_value("../share/sfm_shape_3448.bin"),//可变模型
				"a Morphable Model stored as cereal BinaryArchive")
			("facedetector,f", po::value<fs::path>(&facedetector)->required()->default_value("../share/haarcascade_frontalface_alt2.xml"),//opencv face detector
				"full path to OpenCV's face detector (haarcascade_frontalface_alt2.xml)")
			("landmarkdetector,l", po::value<fs::path>(&landmarkdetector)->required()->default_value("../share/face_landmarks_model_rcr_68.bin"),
				"learned landmark detection model")//68点landmark
			("mapping,p", po::value<fs::path>(&mappingsfile)->required()->default_value("../share/ibug_to_sfm.txt"),
				"landmark identifier to model vertex number mapping")
			("model-contour,c", po::value<fs::path>(&contourfile)->required()->default_value("../share/sfm_model_contours.json"),
				"file with model contour indices")
			("edge-topology,e", po::value<fs::path>(&edgetopologyfile)->required()->default_value("../share/sfm_3448_edge_topology.json"),
				"file with model's precomputed edge topology")
			("blendshapes,b", po::value<fs::path>(&blendshapesfile)->required()->default_value("../share/expression_blendshapes_3448.bin"),
				"file with blendshapes")
			("input,i", po::value<fs::path>(&inputvideo),
				"input video file. If not specified, camera 0 will be used.")
			;
		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
		if (vm.count("help")) {
			cout << "Usage: 4dface [options]" << endl;
			cout << desc;
			return EXIT_FAILURE;
		}
		po::notify(vm);
	}
	catch (const po::error& e) {
		cout << "Error while parsing command-line arguments: " << e.what() << endl;
		cout << "Use --help to display a list of options." << endl;
		return EXIT_FAILURE;
	}

	// Load the Morphable Model and the LandmarkMapper:
        const morphablemodel::MorphableModel morphable_model = morphablemodel::load_model(modelfile.string());//load 可变模型
        const core::LandmarkMapper landmark_mapper = mappingsfile.empty() ? core::LandmarkMapper() : core::LandmarkMapper(mappingsfile.string());

        const fitting::ModelContour model_contour = contourfile.empty() ? fitting::ModelContour() : fitting::ModelContour::load(contourfile.string());
	const fitting::ContourLandmarks ibug_contour = fitting::ContourLandmarks::load(mappingsfile.string());//load LandmarkMapper

	rcr::detection_model rcr_model;
	// Load the landmark detection model:
	try {
		rcr_model = rcr::load_detection_model(landmarkdetector.string());
	}
	catch (const cereal::Exception& e) {
		cout << "Error reading the RCR model " << landmarkdetector << ": " << e.what() << endl;
		return EXIT_FAILURE;
	}

	// Load the face detector from OpenCV:
	cv::CascadeClassifier face_cascade;
	if (!face_cascade.load(facedetector.string()))
	{
		cout << "Error loading the face detector " << facedetector << "." << endl;
		return EXIT_FAILURE;
	}

	cv::VideoCapture cap;
	if (inputvideo.empty()) {
		cap.open(0); // no file given, open the default camera
	}
	else {
		has_movie = 1;
		cap.open(inputvideo.string());
	}
	if (!cap.isOpened()) {
		cout << "Couldn't open the given file or camera 0." << endl;
		return EXIT_FAILURE;
	}
	//------------------------------------------------------------Load结束--------------------------------------------------------
	const morphablemodel::Blendshapes blendshapes = morphablemodel::load_blendshapes(blendshapesfile.string());

	const morphablemodel::EdgeTopology edge_topology = morphablemodel::load_edge_topology(edgetopologyfile.string());

	cv::namedWindow("video", 1);
	cv::namedWindow("render", 1);

	Mat frame, unmodified_frame, unmodified_frame_background;
	Mat frame_video, unmodified_frame_video;
	bool have_face = false;
	bool merge_texture = true;		//false说明使用之前的texture，true说明还要继续merge
	rcr::LandmarkCollection<Vec2f> current_landmarks;
	rcr::LandmarkCollection<Vec2f> current_landmarks_video;
	Rect current_facebox;
	WeightedIsomapAveraging isomap_averaging(60.f); // merge all triangles that are facing <60?towards the camera
	PcaCoefficientMerging pca_shape_merging;
	Mat movie_closeeye_isomap;
	Mat movie_openeye_isomap;
	WeightedIsomapAveraging movie_closeeye_isomap_averaging(60.f); //视频中闭眼部分的整体map
	WeightedIsomapAveraging movie_openeye_isomap_averaging(60.f);
	Mat imageROI;
	int face_position_x, face_position_y;

	for (;;)
	{
		cap >> frame; // get a new frame from camera
		if (frame.empty()) { // stop if we're at the end of the video
			break;
		}

		// We do a quick check if the current face's width is <= 50 pixel. If it is, we re-initialise the tracking with the face detector.
		if (have_face && get_enclosing_bbox(rcr::to_row(current_landmarks)).width <= 50) {
			cout << "Reinitialising because the face bounding-box width is <= 50 px" << endl;
			have_face = false;
		}

		unmodified_frame = frame.clone();
		//unmodified_frame_background = frame.clone();
		if (!have_face) {
			// Run the face detector and obtain the initial estimate using the mean landmarks:
			vector<Rect> detected_faces;
			vector<Rect> detected_faces_video;
			face_cascade.detectMultiScale(unmodified_frame, detected_faces, 1.2, 2, 0, cv::Size(110, 110));
			if (detected_faces.empty()) {
				cv::imshow("video", frame);
				cv::waitKey(30);
				continue;
			}
			cv::rectangle(frame, detected_faces[0], { 255, 0, 0 });
			// Rescale the V&J facebox to make it more like an ibug-facebox:
			// (also make sure the bounding box is square, V&J's is square)
			Rect ibug_facebox = rescale_facebox(detected_faces[0], 0.85, 0.2);

			current_landmarks = rcr_model.detect(unmodified_frame, ibug_facebox);
			rcr::draw_landmarks(frame, current_landmarks, { 0, 0, 255 }); // red, initial landmarks

			have_face = true;
		}
		else {
			// We already have a face - track and initialise using the enclosing bounding
			// box from the landmarks from the last frame:
			auto enclosing_bbox = get_enclosing_bbox(rcr::to_row(current_landmarks));
			enclosing_bbox = make_bbox_square(enclosing_bbox);
			current_landmarks = rcr_model.detect(unmodified_frame, enclosing_bbox);




			Landmark_Pos << "----------------------眼睛眨眼检测----------------------------" << endl;

				Landmark_Pos << "y:41-37:"<< current_landmarks[41].coordinates - current_landmarks[37].coordinates << endl;
				Landmark_Pos << "y:40-38:" << current_landmarks[40].coordinates - current_landmarks[38].coordinates << endl;
				Landmark_Pos << "x:39-36:" << current_landmarks[39].coordinates - current_landmarks[36].coordinates << endl;
				Landmark_Pos << "y/x" << ((current_landmarks[41].coordinates[1] - current_landmarks[37].coordinates[1]) +
					(current_landmarks[40].coordinates[1] - current_landmarks[38].coordinates[1])) /
					(current_landmarks[39].coordinates[0] - current_landmarks[36].coordinates[0]) << endl;
				//生成人脸重新渲染时的起始坐标，用鼻子当作中心点
				face_position_x = int(current_landmarks[30].coordinates[0]);
				face_position_y = int(current_landmarks[30].coordinates[1]);

			rcr::draw_landmarks(frame, current_landmarks, { 255, 255, 0 }); // blue, the new optimised landmarks 画landmark
		}

		// Fit the 3DMM:
		fitting::RenderingParameters rendering_params;
		vector<float> shape_coefficients, blendshape_coefficients;
		vector<float> shape_c;

		vector<Eigen::Vector2f> image_points;
		core::Mesh mesh;
		std::tie(mesh, rendering_params) = fitting::fit_shape_and_pose(morphable_model, blendshapes, rcr_to_eos_landmark_collection(current_landmarks), landmark_mapper, unmodified_frame.cols, unmodified_frame.rows, edge_topology, ibug_contour, model_contour, 3, 5, 15.0f, cpp17::nullopt, shape_coefficients, blendshape_coefficients, image_points);

		// Draw the 3D pose of the face:
		//draw_axes_topright(glm::eulerAngles(rendering_params.get_rotation())[0], glm::eulerAngles(rendering_params.get_rotation())[1], glm::eulerAngles(rendering_params.get_rotation())[2], frame);
		
		// Wireframe rendering of mesh of this frame (non-averaged):
		//render::draw_wireframe(frame, mesh, rendering_params.get_modelview(), rendering_params.get_projection(), fitting::get_opencv_viewport(frame.cols, frame.rows));

		float result = ((current_landmarks[41].coordinates[1] - current_landmarks[37].coordinates[1]) +
			(current_landmarks[40].coordinates[1] - current_landmarks[38].coordinates[1])) /
			(current_landmarks[39].coordinates[0] - current_landmarks[36].coordinates[0]);

		// Extract the texture using the fitted mesh from this frame:
                const Eigen::Matrix<float, 3, 4> affine_cam = fitting::get_3x4_affine_camera_matrix(rendering_params, frame.cols, frame.rows);
                //当前帧的ISOMAP
				Mat isomap = core::to_mat(render::extract_texture(mesh, affine_cam, core::from_mat(unmodified_frame), true, render::TextureInterpolation::NearestNeighbour, 512));
			
		// Merge the isomaps - add the current one to the already merged ones:
				Mat merged_isomap;		//整体的isomap
				
				//如果现在需要进行merge（在isomap生成的阶段）
				if (merge_texture == true) {
					cout << result << endl;
					if (has_movie == 1 && result < 0.45)
					{
						cout << "闭眼" << endl;
						movie_closeeye_isomap = movie_closeeye_isomap_averaging.add_and_merge(isomap);
						cout << "保存结束" << endl;
					}
					else {
						movie_openeye_isomap = movie_openeye_isomap_averaging.add_and_merge(isomap);
					}
					merged_isomap = isomap_averaging.add_and_merge(isomap);
				}
				else {
					//。如果现在不需要merge（isomap已经存好了，只需要用户操控姿态和表情）
					//。需要读取之前的isomap
				}
				
				
				
		// Same for the shape:
		shape_coefficients = pca_shape_merging.add_and_merge(shape_coefficients);
		shape_c = pca_shape_merging.get_merge();
                const Eigen::VectorXf merged_shape =
                    morphable_model.get_shape_model().draw_sample(shape_coefficients) +
                    to_matrix(blendshapes) *
                    Eigen::Map<const Eigen::VectorXf>(blendshape_coefficients.data(), blendshape_coefficients.size());
		const core::Mesh merged_mesh = morphablemodel::sample_to_mesh(merged_shape, morphable_model.get_color_model().get_mean(), morphable_model.get_shape_model().get_triangle_list(), morphable_model.get_color_model().get_triangle_list(), morphable_model.get_texture_coordinates());
		
		// Render the model in a separate window using the estimated pose, shape and merged texture:
		core::Image4u rendering;
		auto modelview_no_translation = rendering_params.get_modelview();
		modelview_no_translation[3][0] = 0;
		modelview_no_translation[3][1] = 0;
		
		if (result < 0.4) {
			std::tie(rendering, std::ignore) = render::render(merged_mesh, modelview_no_translation, glm::ortho(-130.0f, 130.0f, -130.0f, 130.0f), 256, 256, render::create_mipmapped_texture(close_isomap), true, false, false);

		}
		else if (merge_texture == true) {
			std::tie(rendering, std::ignore) = render::render(merged_mesh, modelview_no_translation, glm::ortho(-130.0f, 130.0f, -130.0f, 130.0f), 256, 256, render::create_mipmapped_texture(open_isomap), true, false, false);
			//std::tie(rendering, std::ignore) = render::render(merged_mesh, modelview_no_translation, glm::ortho(-130.0f, 130.0f, -130.0f, 130.0f), 256, 256, render::create_mipmapped_texture(merged_isomap), true, false, false);
		}
		else {
			std::tie(rendering, std::ignore) = render::render(merged_mesh, modelview_no_translation, glm::ortho(-130.0f, 130.0f, -130.0f, 130.0f), 256, 256, render::create_mipmapped_texture(isomap_former), true, false, false);
		}
		
		cv::imshow("input_video", frame);
		//----------------------------------脸部渲染到背景-------------------------------------//
		
		Mat face = core::to_mat(rendering);
		cv::imwrite("render.jpg", face);
		Mat test = cv::imread("render.jpg", 1);
		Mat mask = cv::imread("render.jpg", 0);
		if (has_movie == 1) {
			if (face_position_x > 128 && face_position_y > 128) {
				cout << "位置" << face_position_x << " " << face_position_y << endl;
				imageROI = frame(cv::Rect(face_position_x - 128, face_position_y - 128, face.rows, face.cols));

			}
		}
		//face.copyTo(imageROI)
		test.copyTo(imageROI, mask);
		//open_isomap.copyTo(imageROI);
		
		
		//----------------------------------窗口显示-------------------------------------//
		cv::imshow("render", core::to_mat(rendering));
		//cv::imwrite("render.jpg", core::to_mat(rendering));
		cv::imshow("video", frame);
		auto key = cv::waitKey(5);

		if (key == 'q') break;
		if (key == 'r') {
			have_face = false;
			merge_texture = false;
			isomap_former = merged_isomap;
			//isomap_averaging = WeightedIsomapAveraging(60.f);
			has_movie = 0;
			cap.release();
			cap.open(0);
		}
		if (key == 't') {
			have_face = false;
			isomap_averaging = WeightedIsomapAveraging(60.f);
		}
		/*if (key == 's') {
			// save an obj + current merged isomap to the disk:
			const core::Mesh neutral_expression = morphablemodel::sample_to_mesh(morphable_model.get_shape_model().draw_sample(shape_coefficients), morphable_model.get_color_model().get_mean(), morphable_model.get_shape_model().get_triangle_list(), morphable_model.get_color_model().get_triangle_list(), morphable_model.get_texture_coordinates());
                        core::write_textured_obj(neutral_expression, "current_merged.obj");
			cv::imwrite("current_merged.isomap.png", merged_isomap);
		}*/
		if (key == 's') {
			//保存闭眼的map
			// save an obj + current merged isomap to the disk:
			const core::Mesh neutral_expression = morphablemodel::sample_to_mesh(morphable_model.get_shape_model().draw_sample(shape_coefficients), morphable_model.get_color_model().get_mean(), morphable_model.get_shape_model().get_triangle_list(), morphable_model.get_color_model().get_triangle_list(), morphable_model.get_texture_coordinates());
			//core::write_textured_obj(neutral_expression, "current_merged.obj");
			cv::imwrite("movie_closeeye_isomap.isomap.png", movie_closeeye_isomap);
		}
		if (key == 'a') {
			//保存睁眼的map
			// save an obj + current merged isomap to the disk:
			const core::Mesh neutral_expression = morphablemodel::sample_to_mesh(morphable_model.get_shape_model().draw_sample(shape_coefficients), morphable_model.get_color_model().get_mean(), morphable_model.get_shape_model().get_triangle_list(), morphable_model.get_color_model().get_triangle_list(), morphable_model.get_texture_coordinates());
			//core::write_textured_obj(neutral_expression, "current_merged.obj");
			cv::imwrite("movie_openeye_isomap.isomap.png", movie_openeye_isomap);
		}
		if (key == 'b') {
			//保存视频中的欸经部分图片
			if (has_movie == 1) {
				cv::imwrite("background.png", frame);
			}

		}
	}

	return EXIT_SUCCESS;
};

/**
 * @brief Draws 3D axes onto the top-right corner of the image. The
 * axes are oriented corresponding to the given angles.
 *
 * @param[in] r_x Pitch angle, in radians.
 * @param[in] r_y Yaw angle, in radians.
 * @param[in] r_z Roll angle, in radians.
 * @param[in] image The image to draw onto.
 */
void draw_axes_topright(float r_x, float r_y, float r_z, cv::Mat image)
{
	const glm::vec3 origin(0.0f, 0.0f, 0.0f);
	const glm::vec3 x_axis(1.0f, 0.0f, 0.0f);
	const glm::vec3 y_axis(0.0f, 1.0f, 0.0f);
	const glm::vec3 z_axis(0.0f, 0.0f, 1.0f);

	const auto rot_mtx_x = glm::rotate(glm::mat4(1.0f), r_x, glm::vec3{ 1.0f, 0.0f, 0.0f });
	const auto rot_mtx_y = glm::rotate(glm::mat4(1.0f), r_y, glm::vec3{ 0.0f, 1.0f, 0.0f });
	const auto rot_mtx_z = glm::rotate(glm::mat4(1.0f), r_z, glm::vec3{ 0.0f, 0.0f, 1.0f });
	const auto modelview = rot_mtx_z * rot_mtx_x * rot_mtx_y;

	const auto viewport = fitting::get_opencv_viewport(image.cols, image.rows);
	const float aspect = static_cast<float>(image.cols) / image.rows;
	const auto ortho_projection = glm::ortho(-3.0f * aspect, 3.0f * aspect, -3.0f, 3.0f);
	const auto translate_topright = glm::translate(glm::mat4(1.0f), glm::vec3(0.7f, 0.65f, 0.0f));
	const auto o_2d = glm::project(origin, modelview, translate_topright * ortho_projection, viewport);
	const auto x_2d = glm::project(x_axis, modelview, translate_topright * ortho_projection, viewport);
	const auto y_2d = glm::project(y_axis, modelview, translate_topright * ortho_projection, viewport);
	const auto z_2d = glm::project(z_axis, modelview, translate_topright * ortho_projection, viewport);
	cv::line(image, cv::Point2f{ o_2d.x, o_2d.y }, cv::Point2f{ x_2d.x, x_2d.y }, { 0, 0, 255 });
	cv::line(image, cv::Point2f{ o_2d.x, o_2d.y }, cv::Point2f{ y_2d.x, y_2d.y }, { 0, 255, 0 });
	cv::line(image, cv::Point2f{ o_2d.x, o_2d.y }, cv::Point2f{ z_2d.x, z_2d.y }, { 255, 0, 0 });
};
