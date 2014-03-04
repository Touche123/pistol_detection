//
//  main.cpp
//  PistolDetection
//
//  Created by John Doherty on 2/10/14.
//  Copyright (c) 2014 John Doherty, Aaron Damashek. All rights reserved.
//

#include "opencv2/contrib/contrib.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "../dlib-18.6/dlib/svm.h"
#include <iostream>
#include <fstream>

using namespace std;
using namespace cv;
using namespace dlib;

Vector<Vector<int>> truth;
const int numPolygons = 10;
const int features = numPolygons*2;
double matchThreshold = .15;

typedef matrix<double, numPolygons, 1> subImageResults;
typedef radial_basis_kernel<subImageResults> kernel;
typedef decision_function<kernel> dec_funct_type;
typedef normalized_function<dec_funct_type> funct;

struct image_truth{
    Vector<Mat> Images;
    Vector<int> imageTruths;
};

struct chamferResult{
    bool found;
    double cost;
};

/*Read in true values of whether or not a gun is in a specified image*/
void populateTruth(){
    //Auto-file read in
    ifstream input( "./truth.txt" );
    std::string line;
    while (std::getline(input, line)){
        Vector<int> currFolder;
        for(int num = 0; num < line.length(); num++){
            int currNum = atoi(&line[num]);
            currFolder.push_back(currNum);
        }
        truth.push_back(currFolder);
    }
}

/*Read in images and store the ground truth of whether a gun is associated with the image*/
image_truth readInImages(){
    image_truth images;
    for(int i = 1; i <= 120; i++){
        if(i == 97) break; //Ignore this folder of images
        int imgNum = 1;
        while(true){
            string folder = to_string(i);
            string pic = to_string(imgNum);
            if(i < 10) folder = "0" + folder;
            if(imgNum < 10) pic = "0" + pic;
            string fileLocation = "../../images/X0" + folder + "/X0" + folder + "_" + pic + ".png";
            Mat img = imread(fileLocation, CV_LOAD_IMAGE_GRAYSCALE);
            Mat cimg;
            cvtColor(img, cimg, CV_GRAY2BGR);
            if(!img.data) break;
            images.Images.push_back(img);
            images.imageTruths.push_back(truth[i][imgNum]);
            imgNum++;
        }
    }
    return images;
}

/*Return whether or not a gun was identified using basic chamfer*/
chamferResult basicChamfer(Mat img, Mat tpl){
    Canny(img, img, 100, 300, 3);
    Canny(tpl, tpl, 100, 300, 3);
    
    
    std::vector<std::vector<Point> > results;
    std::vector<float> costs;
    
    int best = chamerMatching(img, tpl, results, costs);
    chamferResult result;
    if( best < 0 || costs[best] < matchThreshold) {
        result.found=false;
        //cout << "not found;\n";
        //return false;
    }else{
        result.found = true;
        result.cost = costs[best];
    }
    //return true;
    return result;
}

/*Split image into grid of subimages*/
Vector<Mat> splitIntoImages(Mat img, int rows = 4, int cols = 4){
    Vector<Mat> subImages;
    int rowSize = ceil((float)img.rows / rows);
    int colSize = ceil((float)img.cols / cols);
    
    for (int r = 0; r < rows; r ++) {
        for (int c = 0; c < cols; c ++) {
            subImages.push_back(img(Range((r*rowSize), min(((r+1) * rowSize), img.rows)),
                                    Range((c*colSize), min(((c+1) * colSize), img.cols))).clone());
        }
    }
    return subImages;
}

/*Return whether or not a gun is identified based on identification of a majority of subimages*/
chamferResult votingChamfer(Mat img, Mat tpl){
    chamferResult result;
	Vector<Mat> subPolygons = splitIntoImages(img);
	int detected = 0;
	for(int i = 0; i < numPolygons; i++){
        chamferResult subresult = basicChamfer(subPolygons[i], tpl);
		if(subresult.found==true) detected += 1;
	}
	if(detected > numPolygons/2){
        result.found = true;
    }else{
        result.found = false;
    }
    return result;
}

/*Calculate the function to be used for ML using training samples*/
funct setUpMLChamfer(Mat tpl, Vector<Mat> trainingImages, Vector<int> imageTruths){
	Vector<Vector<int>> trainingSamples;
    Vector<int> found;
    

    std::vector<subImageResults> samples;
    std::vector<double> labels;//Ground truth
    
    bool samplesTested = false;
    if(!samplesTested){
        ofstream results;
        results.open ("./subImageResults");
        
        for(int j = 0; j < trainingImages.size(); j++){
            Vector<Mat> subPolygons = splitIntoImages(trainingImages[j]);
            for(int i = 0; i < numPolygons; i++){
                //Include scores??????????
                chamferResult subresult = basicChamfer(subPolygons[i], tpl);
                if(subresult.found){
                    found.push_back(1);//1 is found
                    //found.push_back(subresult.cost);
                }else{
                    found.push_back(0);//0 is not found
                    //found.push_back(subresult.cost);
                }
            }
            trainingSamples.push_back(found);
            
            //Write samples to file
            std::stringstream result;
            std::copy(found.begin(), found.end(), std::ostream_iterator<int>(result));
            results << result.str() << endl;
        }
        
        results.close();
    }else{
        //read samples from file
        //How would this work with costs as well??????
        ifstream input( "./subImageResults.txt" );
        std::string line;
        while (std::getline(input, line)){
            Vector<int> currImage;
            for(int num = 0; num < line.length(); num++){
                int currNum = atoi(&line[num]);
                currImage.push_back(currNum);
            }
            trainingSamples.push_back(currImage);
        }
    }
    
    for(int i = 0; i < trainingSamples.size(); i++){
        subImageResults sample;
        for(int j = 0; j < numPolygons; j++){
            sample(j) = trainingSamples[i][j];
            samples.push_back(sample);
        }
        labels.push_back(imageTruths[i]);
    }
    
    //Normalize samples
    vector_normalizer<subImageResults> normalizer;
    normalizer.train(samples);
    for (unsigned long i = 0; i < samples.size(); i++){
        samples[i] = normalizer(samples[i]);
    }
    
    randomize_samples(samples, labels);
    // The nu parameter has a maximum value that is dependent on the ratio of the +1 to -1
    // labels in the training data.  This function finds that value.
    const double max_nu = maximum_nu(labels);
    // here we make an instance of the svm_nu_trainer object that uses our kernel type.
    //svm_c_ekm_trainer<kernel> trainer;
    svm_nu_trainer<kernel> trainer;
    cout << "doing cross validation" << endl;
    for (double gamma = 0.00001; gamma <= 1; gamma *= 5)
    {
        for (double nu = 0.00001; nu < max_nu; nu *= 5)
        {
            // tell the trainer the parameters we want to use
            trainer.set_kernel(kernel(gamma));
            trainer.set_nu(nu);
            
            cout << "gamma: " << gamma << "    nu: " << nu << endl;
            // Print out the cross validation accuracy for 3-fold cross validation using
            // the current gamma and nu.  cross_validate_trainer() returns a row vector.
            // The first element of the vector is the fraction of +1 training examples
            // correctly classified and the second number is the fraction of -1 training
            // examples correctly classified.
            cout << "cross validation accuracy: " << cross_validate_trainer(trainer, samples, labels, 3) << endl;
        }
    }
    
    trainer.set_kernel(kernel(0.15625));
    trainer.set_nu(0.15625);
    
    // Here we are making an instance of the normalized_function object.  This object
    // provides a convenient way to store the vector normalization information along with
    // the decision function we are going to learn.
    funct learned_function;
    learned_function.normalizer = normalizer;  // save normalization information
    learned_function.function = trainer.train(samples, labels); // perform the actual SVM training and save the results
    
    // print out the number of support vectors in the resulting decision function
    cout << "\nnumber of support vectors in our learned_function is "
    << learned_function.function.basis_vectors.size() << endl;

	return learned_function;
}

/*Use trained system to determine whether or not a gun is present based on subimage results*/
bool MLChamfer(Mat img, Mat tpl, funct decisionFunction){
	Vector<Mat> subPolygons = splitIntoImages(img);
    

    //Find results for sub-images
    subImageResults found;
	for(int i = 0; i < numPolygons; i++){
        chamferResult subresult = basicChamfer(subPolygons[i], tpl);
        if(subresult.found){
            found(i) = 1;//1 is found
            //found.push_back(subresult.cost);
        }else{
            found(i) = 0;//0 is not found
            //found.push_back(subresult.cost);
        }
	}
    
    //Do Machine Learning to determine if whole image matches
    double output = decisionFunction(found);
    
    cout << "The classifier output is " << output << endl;
    
    //The decision function will return values
    // >= 0 for samples it predicts are in the +1 class and numbers < 0 for samples it
    // predicts to be in the -1 class.
	return (output >= 0);
}

/*Report the results of a test*/
void reportResults(int falsePositives, int falseNegatives, int correctIdentification, int correctDiscard){
    int sum = falsePositives + falseNegatives + correctDiscard + correctIdentification;
    cout << "False Positives: " << falsePositives << endl;
    cout << "False Negatives: " << falseNegatives << endl;
    cout << "Correct Identifications: " << correctIdentification << endl;
    cout << "Correct Discards: " << correctDiscard << endl;
    if(correctIdentification > 0){
        double precision = correctIdentification/(correctIdentification + falsePositives);
        double recall = correctIdentification/(correctIdentification + falseNegatives);
        double F1 = 2*precision*recall/(precision+recall);
        cout << "Precision: " << precision << endl;
        cout << "Recall: " << recall << endl;
        cout << "F1 Score: " << F1 << endl;
    }
    cout << "Success rate: " << (double)(correctDiscard + correctIdentification)/sum*100 << endl;
}

/*Test the performance of either basic or voting chamfer against all images*/
void testFunction(chamferResult (*chamferFunction)(Mat img, Mat tpl), Mat tpl){//Basic or votingChamfer
    int falsePositives = 0;
    int falseNegatives = 0;
    int correctIdentification = 0;
    int correctDiscard = 0;
    
    image_truth images = readInImages();
    for(int i = 0; i < 10; i++){
    //for(int i = 0; i < images.Images.size(); i++){
        bool gunFound = chamferFunction(images.Images[i], tpl).found; //Basic, votingChamfer
        if(gunFound){
            if(images.imageTruths[i]){
                correctIdentification+=1;
            }else{
                falsePositives+=1;
            }
        }
        if(!gunFound){
            if(!images.imageTruths[i]){
                correctDiscard+=1;
            }else{
                falseNegatives+=1;
            }
        }
    }
    reportResults(falsePositives, falseNegatives, correctIdentification, correctDiscard);
}

/*Test the performance of ML chamfer against all images*/
void testML(Mat tpl){

    int falsePositives = 0;
    int falseNegatives = 0;
    int correctIdentification = 0;
    int correctDiscard = 0;
    
    image_truth images = readInImages();
    randomize_samples(images.Images, images.imageTruths);//Make sure to test that it actually randomizes them
    
    double splitProportion = 7/10;
    std::size_t const splitSize = images.Images.size()*splitProportion; //Make sure to test and see that this works
    Vector<Mat> trainingImages(std::vector<Mat>(images.Images.begin(), images.Images.begin() + splitSize));
    Vector<Mat> testImages(std::vector<Mat>(images.Images.begin() + splitSize, images.Images.end()));
    Vector<int> trainingTruths(std::vector<int>(images.imageTruths.begin(), images.imageTruths.begin() + splitSize));
    Vector<int> testTruths(std::vector<int>(images.imageTruths.begin() + splitSize, images.imageTruths.end()));
    
    //Set-up ML
    funct decision = setUpMLChamfer(tpl, trainingImages, trainingTruths);
    
    //Run ML
    for(int i = 0; i < testImages.size(); i++){
        bool gunFound = MLChamfer(testImages[i], tpl, decision); //MLChamfer
        if(gunFound){
            if(testTruths[i]){
                correctIdentification+=1;
            }else{
                falsePositives+=1;
            }
        }
        if(!gunFound){
            if(!testTruths[i]){
                correctDiscard+=1;
            }else{
                falseNegatives+=1;
            }
        }
    }

    reportResults(falsePositives, falseNegatives, correctIdentification, correctDiscard);
}

int main( int argc, char** argv ) {
    
    if( argc != 1 && argc != 3 ) {
        //help();
        return 0;
    }
    
    //Mat img = imread(argc == 3 ? argv[1] : "./pistol_2.jpg", CV_LOAD_IMAGE_GRAYSCALE);
    Mat img = imread(argc == 3 ? argv[1] : "./logo_in_clutter.png", CV_LOAD_IMAGE_GRAYSCALE);
    Mat cimg;
    cvtColor(img, cimg, CV_GRAY2BGR);
    Mat tpl = imread(argc == 3 ? argv[1] : "./pistol_black_small.jpeg", CV_LOAD_IMAGE_GRAYSCALE);
    
    // if the image and the template are not edge maps but normal grayscale images,
    // you might want to uncomment the lines below to produce the maps. You can also
    // run Sobel instead of Canny.
    
    Canny(img, img, 70, 300, 3);
    Canny(tpl, tpl, 150, 500, 3);
    Vector<Mat>images = splitIntoImages(tpl);

    
    populateTruth();
    testFunction(basicChamfer, tpl);
    //image_truth imgs = readInImages();
    //funct f = setUpMLChamfer(tpl, imgs.Images, imgs.imageTruths);
    //MLChamfer(img, tpl, f);
    return 0;
    
    imshow( "img", img );
    imshow( "template", tpl );
    waitKey(0);
    destroyAllWindows();
    
    std::vector<std::vector<Point> > results;
    std::vector<float> costs;
    imshow( "img", img );
    waitKey(0);
    destroyAllWindows();
    
    /*
     int chamerMatching( Mat& img, Mat& templ,
     CV_OUT vector<vector<Point> >& results, CV_OUT vector<float>& cost,
     double templScale=1, int maxMatches = 20,
     double minMatchDistance = 1.0, int padX = 3,
     int padY = 3, int scales = 5, double minScale = 0.6, double maxScale = 1.6,
     double orientationWeight = 0.5, double truncate = 20);
     */
    
    int best = chamerMatching(img, tpl, results, costs, 1, 50, 1.0, 3, 3, 5, 0.6, 1.6, 0.5, 20);
    //int best = chamerMatching(img, tpl, results, costs);
    if( best < 0 ) {
        cout << "not found;\n";
        return 0;
    }
    size_t j,m = results.size();
    //size_t j = best;
    for(j = 0; j < m; j++) {
        //size_t i, n = results[best].size();
        size_t i, n = results[j].size();
        for( i = 0; i < n; i++ ) {
            Point pt = results[j][i];
            if(pt.inside(Rect(0, 0, cimg.cols, cimg.rows))) {
                if (i == best) {
                    cimg.at<Vec3b>(pt) = Vec3b(255, 0, 0);
                } else {
                    cimg.at<Vec3b>(pt) = Vec3b(0, 255, 0);
                }
            }
            
        }
    }
    
    cout << "Best index: ";
    cout << best;
    cout << "\n";
    cout << "With cost: ";
    cout << costs[best];
    cout << "\n\n";
    
    for (int i = 0; i < costs.size(); i++) {
        cout << costs[i];
        cout << ", ";
    }
    imshow("result", cimg);
    imshow("edges", img);
    waitKey();
    populateTruth();
    testFunction(basicChamfer, tpl);
    //image_truth imgs = readInImages();
    //funct f = setUpMLChamfer(tpl, imgs.Images, imgs.imageTruths);
    //MLChamfer(img, tpl, f);
    return 0;
}

