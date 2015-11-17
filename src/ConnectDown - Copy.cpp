/*   Connect Down function
     
  David Tarboton
  Utah State University  
  July 11, 2012 
  

  This function reads an input grid representing watersheds, reads an ad8 contributing area file, 
  identifies the location of the largest ad8 value as the outlet of the watershed and writes a shapefile at these locations.
  It then reads the flow direction grid and moves the points down direction a designated number of grid cells and writes a shapefile


*/

/*  Copyright (C) 2010  David Tarboton, Utah State University

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License 
version 2, 1991 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

A copy of the full GNU General Public License is included in file 
gpl.html. This is also available at:
http://www.gnu.org/copyleft/gpl.html
or from:
The Free Software Foundation, Inc., 59 Temple Place - Suite 330, 
Boston, MA  02111-1307, USA.

If you wish to use or incorporate this program (or parts of it) into 
other software that does not meet the GNU General Public License 
conditions contact the author to request permission.
David G. Tarboton  
Utah State University 
8200 Old Main Hill 
Logan, UT 84322-8200 
USA 
http://www.engineering.usu.edu/dtarb/ 
email:  dtarb@usu.edu 
*/

//  This software is distributed from http://hydrology.usu.edu/taudem/

// 1/25/14.  Modified to use shapelib by Chris George

#include <mpi.h>
#include <math.h>
#include <queue>
#include "commonLib.h"
#include "linearpart.h"
#include "createpart.h"
#include "tiffIO.h"
//#include "shapelib/shapefil.h"
#include "ConnectDown.h"
using namespace std;

OGRSFDriverH    driver;
OGRDataSourceH  hDSsh, hDSshmoved;
OGRLayerH       hLayersh, hLayershmoved;
OGRFeatureDefnH hFDefnsh,hFDefnshmoved;
OGRFieldDefnH   hFieldDefnsh,hFieldDefnshmoved;
OGRFeatureH     hFeaturesh,hFeatureshmoved;
OGRGeometryH    hGeometrysh, hGeometryshmoved;

	
int connectdown(char *pfile, char *wfile, char *ad8file, char *outletshapefile, char *movedoutletshapefile, int movedist)
{

	MPI_Init(NULL,NULL);{
	int rank,size;
	MPI_Comm_rank(MCW,&rank);
	MPI_Comm_size(MCW,&size);
	if(rank==0)printf("ConnectDown version %s\n",TDVERSION);

	double begin,end;
	//Begin timer
	begin = MPI_Wtime();
	int d1[9] = {-99,0,-1,-1,-1,0,1,1,1};
	int d2[9] = {-99,1,1,0,-1,-1,-1,0,1};

	//load the watershed grid into a linear partition
	//Create tiff object, read and store header info
//	MPI_Abort(MCW,5);
	tiffIO wIO(wfile, LONG_TYPE);
	long wTotalX = wIO.getTotalX();
	long wTotalY = wIO.getTotalY();
	double wdx = wIO.getdxA();
	double wdy = wIO.getdyA();
	OGRSpatialReferenceH hSRSRaster;
    hSRSRaster=wIO.getspatialref();
	if(rank==0)
		{
			float timeestimate=(2e-7*wTotalX*wTotalY/pow((double) size,0.65))/60+1;  // Time estimate in minutes
			//fprintf(stderr,"This run may take on the order of %.0f minutes to complete.\n",timeestimate);
			//fprintf(stderr,"This estimate is very approximate. \nRun time is highly uncertain as it depends on the complexity of the input data \nand speed and memory of the computer. This estimate is based on our testing on \na dual quad core Dell Xeon E5405 2.0GHz PC with 16GB RAM.\n");
			fflush(stderr);
		}


	//Create partition and read data
	tdpartition *wData;
	int32_t nodatav;
	nodatav = (int32_t)wIO.getNodata();
	wData = CreateNewPartition(wIO.getDatatype(), wTotalX, wTotalY, wdx, wdy,nodatav);
	int nx = wData->getnx();
	int ny = wData->getny();
	int wxstart, wystart;  // DGT Why are these declared as int if they are to be used as long
	wData->localToGlobal(0, 0, wxstart, wystart);  //  DGT here no typecast - but 2 lines down there is typecast - why

	wIO.read((long)wxstart, (long)wystart, (long)ny, (long)nx, wData->getGridPointer());
	wData->share();  // fill partition buffers for cross partition lookups

	//load the d8 flow grid into a linear partition
	//Create tiff object, read and store header info
	tiffIO p(pfile, SHORT_TYPE);
	long pTotalX = p.getTotalX();
	long pTotalY = p.getTotalY();
	double pdx = p.getdxA();
	double pdy = p.getdyA();

	//Create partition and read data
	tdpartition *flowData;
	flowData = CreateNewPartition(p.getDatatype(), pTotalX, pTotalY, pdx, pdy, p.getNodata());
	int pnx = flowData->getnx();
	int pny = flowData->getny();
	int pxstart, pystart;
	flowData->localToGlobal(0, 0, pxstart, pystart);
	p.read(pxstart, pystart, pny, pnx, flowData->getGridPointer());

	if(!p.compareTiff(wIO)){
		printf("w and p files not the same size. Exiting \n");
		fflush(stdout);
		MPI_Abort(MCW,4);
	}

	//load the ad8  grid into a linear partition
	//Create tiff object, read and store header info
	tiffIO ad8IO(ad8file, FLOAT_TYPE);
	long ad8TotalX = ad8IO.getTotalX();
	long ad8TotalY = ad8IO.getTotalY();
	double ad8dx = ad8IO.getdxA();
	double ad8dy = ad8IO.getdyA();

	//Create partition and read data
	tdpartition *ad8;
	ad8 = CreateNewPartition(ad8IO.getDatatype(), ad8TotalX, ad8TotalY, ad8dx, ad8dy, ad8IO.getNodata());
	int ad8nx = ad8->getnx();
	int ad8ny = ad8->getny();
	int ad8xstart, ad8ystart;
	ad8->localToGlobal(0, 0, ad8xstart, ad8ystart);
	ad8IO.read(ad8xstart, ad8ystart, ad8ny, ad8nx, ad8->getGridPointer());

	if(!ad8IO.compareTiff(wIO)){
		printf("ad8 and w files not the same size. Exiting \n");
		MPI_Abort(MCW,4);
	}





	// Parse to find maximum
	int32_t wmax=0;  // This assumes that the maximum will be positive
	long i,j;	
	for(j=0; j<ny; j++) {
			for(i=0; i<nx; i++ ) 
			{					
				if(!wData->isNodata(i,j)) 
				{
					int32_t templong;
					wData->getData(i,j,templong);
					if(templong > wmax)wmax=templong;
				}				
			}			
	}
	

 
		//total up all the nodes and all the finished nodes
	int maxall;
	MPI_Allreduce(&wmax, &maxall, 1, MPI_LONG, MPI_MAX, MCW);
	//  Allocate arrays
	maxall=maxall+1;  // +1 so that array from 0:wmax as a subscript is valid
	int *wfound = new int[maxall];  
	double *wi=new double[maxall];
	double *wj=new double[maxall];
	float *ad8max=new float[maxall];
	int *wid = new int[maxall];
//  Initialize found and max array found 0 means not found in this partition, 1 means found
	for(i=0; i<maxall; i++)
	{
		wfound[i]=0;
		ad8max[i]=0;
	}

	//  Loop over grid recording maximum in each watershed
	for(j=0; j<ny; j++) {
			for(i=0; i<nx; i++ ) 
			{					
				if(!wData->isNodata(i,j)) 
				{
					int32_t ii;
					wData->getData(i,j,ii);
					if(ii == 469)
						ii=ii;
					float tempFloat;
					ad8->getData(i,j,tempFloat);
					if(wfound[ii]==0) //  First encounter
					{
						wfound[ii]=1;
						int gx,gy;  //  Global x and y (col and row) coordinates
						ad8->localToGlobal(i,j,gx,gy);
						wIO.globalXYToGeo(gx,gy,wi[ii],wj[ii]);
						//wi[ii]=i;
						//wj[ii]=j;
						ad8max[ii]=tempFloat;
					}
					else
					{
						if(tempFloat > ad8max[ii])
						{
							int gx,gy;  //  Global x and y (col and row) coordinates
							ad8->localToGlobal(i,j,gx,gy);
							wIO.globalXYToGeo(gx,gy,wi[ii],wj[ii]);
							//wi[ii]=i;
							//wj[ii]=j;
							ad8max[ii]=tempFloat;
						}
					}
				}
			}
	}

	//  if not root process mpisend the results
    int *ptr;
    int place;
	int *buf;
	float *fbuf;
	double *dbuf;
    int bsize=(maxall)*sizeof(int)+MPI_BSEND_OVERHEAD;  
    int fsize=(maxall)*sizeof(float)+MPI_BSEND_OVERHEAD;  
	int dsize=(maxall)*sizeof(double)+MPI_BSEND_OVERHEAD;  
    buf = new int[bsize];
	fbuf = new float[fsize];
	dbuf = new double[dsize];
    int nxy=0;  // Number of points found

	if(rank>0){
			MPI_Buffer_attach(buf,bsize);
			MPI_Bsend(wfound, maxall, MPI_INT, 0, 0, MCW);
			MPI_Buffer_detach(&ptr,&place);

			MPI_Buffer_attach(buf,dsize);
			MPI_Bsend(wi, maxall, MPI_DOUBLE, 0, 1, MCW);
			MPI_Buffer_detach(&ptr,&place);

		    MPI_Buffer_attach(buf,dsize);
			MPI_Bsend(wj, maxall, MPI_DOUBLE, 0, 2, MCW);
			MPI_Buffer_detach(&ptr,&place);

			MPI_Buffer_attach(fbuf,fsize);
			MPI_Bsend(ad8max, maxall, MPI_FLOAT, 0, 3, MCW);
			MPI_Buffer_detach(&ptr,&place);
	}
	else  	//  if root process receive the results and note the overall maximum
	{
		int *rwfound = new int[maxall];  
		double *rwi=new double[maxall];
		double *rwj=new double[maxall];
		float *rad8max=new float[maxall];
		MPI_Status status;
		for(i=1; i<size; i++)
		{
			MPI_Recv(rwfound, maxall, MPI_INT, i, 0, MCW, &status);
			MPI_Recv(rwi, maxall, MPI_DOUBLE, i, 1, MCW, &status);
			MPI_Recv(rwj, maxall, MPI_DOUBLE, i, 2, MCW, &status);
			MPI_Recv(rad8max, maxall, MPI_FLOAT, i, 3, MCW, &status);
			for(int ii=0; ii<maxall; ii++)
			{
				if(rwfound[ii]>0)
					if(rad8max[ii]>ad8max[ii])
					{
						ad8max[ii]=rad8max[ii];
						wi[ii]=rwi[ii];
						wj[ii]=rwj[ii];
						wfound[ii]=1;
					}
			}
		}



		for(int ii=0; ii<maxall; ii++)
			{
					if(wfound[ii]>0)
					{
						wi[nxy]=wi[ii];
						wj[nxy]=wj[ii];
						wid[nxy]=ii;
						ad8max[nxy]=ad8max[ii];
						nxy=nxy+1;
					}
		}
	}
	//  mpi_bcast the coordinates to all processes
	MPI_Bcast(&nxy, 1, MPI_INT, 0, MCW);
	if(nxy==0)
	{  // Attempt to exit gracefully on all processors
		if(rank==0)
			printf("No points found\n\n");
	
		MPI_Finalize();
	}
	double *xnode, *ynode, *origxnode, *origynode;
	long * dist_moved;
	
	xnode = new double[nxy];
	ynode = new double[nxy];
	origxnode = new double[nxy];
	origynode = new double[nxy];
//	ismoved = new long[nxy];  // DGT decided not needed 
	dist_moved = new long[nxy];
	int *part_has = new int[nxy];  // Variable to keep track of which partition currently has control over this outlet
	int *widdown = new int[nxy];  // Down identifiers

	if(rank==0){
		for(i=0; i<nxy; i++) {
			
			xnode[i] = wi[i];  //shp->getPoint(0).getX();
			ynode[i] = wj[i];  //shp->getPoint(0).getY();
			origxnode[i]=xnode[i];
			origynode[i]=ynode[i];
			//  Initializing
//			ismoved[i] = 0;
			dist_moved[i] = 0;
			part_has[i]=-1;  // initialize part_has to -1 for all points.  This will be set to rank later
		}
	}

	MPI_Bcast(xnode, nxy, MPI_DOUBLE, 0, MCW);
	MPI_Bcast(ynode, nxy, MPI_DOUBLE, 0, MCW);
//	MPI_Bcast(ismoved, nxy, MPI_LONG, 0, MCW);
	MPI_Bcast(dist_moved, nxy, MPI_LONG, 0, MCW);
	MPI_Bcast(part_has, nxy, MPI_INT, 0, MCW);
	MPI_Bcast(wid, nxy, MPI_INT, 0, MCW);

	//  move outlets a prescribed distance
	// oooooooooooooooooooooo  begin processing

    int *outletsX, *outletsY;
	int tx,ty;
	short td;
    outletsX = new int[nxy];
    outletsY = new int[nxy];
	int done = 0;
	int localdone = 0;
	int localnodes;
	int totalnodes;
	int totaldone;
	short dirn;
	int nextx,nexty;
	
    //Convert geo coords to grid coords
    for( i=0; i<nxy; i++)
    	p.geoToGlobalXY(xnode[i], ynode[i], outletsX[i], outletsY[i]);

	while(!done){
        for( i=0; i<nxy; i++){
			flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
			if(flowData->isInPartition(tx,ty))part_has[i]=rank;  // grab control if in partition
			else part_has[i]=-1;  // some other partition has control
			if(flowData->isInPartition(tx,ty) &&dist_moved[i]>=0){
					dirn = flowData->getData(tx,ty,dirn);
					int32_t tempLong;
					widdown[i] = wData->getData(tx,ty,tempLong);  //  initialize down identifier
					if(dirn>=1 && dirn<=8 && dist_moved[i] < movedist){
						nextx=outletsX[i]+d2[dirn];
						nexty=outletsY[i]+d1[dirn];
						if(nextx>=0 && nexty>=0 && nextx< pTotalX && nexty< pTotalY){  // If is within global domain
							outletsX[i]=nextx;
							outletsY[i]=nexty;
							//ismoved[i]=1;
							dist_moved[i]++;
							flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
							widdown[i] = wData->getData(tx,ty,tempLong);  //  update down identifier
							//printf("Outlet: %d, x: %d, y: %d\n",i,nextx,nexty);
						}else{
							// moved off the map
							dist_moved[i]=-1;
						}
					}else{
						//flow data not a direction
						dist_moved[i]=-1;
					}
			} 
		}
			//share data with neighbors
		//this code is linear partition specific
        int * toutletsX = new int[nxy];
        int * toutletsY = new int[nxy];
	//	long * tismoved = new long[nxy];
		long * tdist_moved = new long[nxy];
		int * twiddown = new int[nxy];
		MPI_Status status;
        	int *ptr;
        	int place;
        	int *buf;
			long *lbuf;
        	int bsize=nxy*sizeof(int)+MPI_BSEND_OVERHEAD;  
        	int lsize=nxy*sizeof(long)+MPI_BSEND_OVERHEAD;  
        	buf = new int[bsize];
			lbuf = new long[lsize];
        	
		int txn, tyn;

		int dest = rank-1;
		if (dest<0)dest+=size;  
		if(size>1){
			MPI_Buffer_attach(buf,bsize);
			MPI_Bsend(outletsX, nxy, MPI_INT, dest, 0, MCW);
			MPI_Buffer_detach(&ptr,&place);
			MPI_Buffer_attach(buf,bsize);
			MPI_Bsend(outletsY, nxy, MPI_INT, dest, 1, MCW);
			MPI_Buffer_detach(&ptr,&place);
			MPI_Buffer_attach(buf,bsize);
			MPI_Bsend(widdown, nxy, MPI_INT, dest, 2, MCW);
			MPI_Buffer_detach(&ptr,&place);
			MPI_Buffer_attach(lbuf,lsize);
			MPI_Bsend(dist_moved, nxy, MPI_LONG, dest, 3, MCW);
			MPI_Buffer_detach(&ptr,&place);
			MPI_Recv(toutletsX, nxy, MPI_INT, MPI_ANY_SOURCE, 0, MCW, &status);
			MPI_Recv(toutletsY, nxy, MPI_INT, MPI_ANY_SOURCE, 1, MCW, &status);
			MPI_Recv(twiddown, nxy, MPI_INT, MPI_ANY_SOURCE, 2, MCW, &status);
			MPI_Recv(tdist_moved, nxy, MPI_LONG, MPI_ANY_SOURCE, 3, MCW, &status);
		}
        for( i=0; i<nxy; i++){
			//if a node in the temp arrays belongs to us, and we don't already have it, get it
			flowData->globalToLocal(toutletsX[i], toutletsY[i], txn, tyn);  //DGT with single partition this is being passed uninitialized information
			flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
			if(flowData->isInPartition(txn,tyn) && !(part_has[i]==rank)){  // if an outlet not in partition is now in partition - grab it
				outletsX[i]=toutletsX[i];
				outletsY[i]=toutletsY[i];
				part_has[i]=rank;
				dist_moved[i]=tdist_moved[i];
				widdown[i]=twiddown[i];
			}
		}

		dest = rank+1;
		if (dest>=size)dest-=size;  //DGT Why is this here.  
		if(size>1){
			MPI_Buffer_attach(buf,bsize);
			MPI_Bsend(outletsX, nxy, MPI_INT, dest, 4, MCW);
			MPI_Buffer_detach(&ptr,&place);
			MPI_Buffer_attach(buf,bsize);
			MPI_Bsend(outletsY, nxy, MPI_INT, dest, 5, MCW);
			MPI_Buffer_detach(&ptr,&place);
			MPI_Buffer_attach(buf,bsize);
			MPI_Bsend(widdown, nxy, MPI_INT, dest, 6, MCW);
			MPI_Buffer_detach(&ptr,&place);
			MPI_Buffer_attach(lbuf,lsize);
			MPI_Bsend(dist_moved, nxy, MPI_LONG, dest, 7, MCW);
			MPI_Buffer_detach(&ptr,&place);
			MPI_Recv(toutletsX, nxy, MPI_INT, MPI_ANY_SOURCE, 4, MCW, &status);
			MPI_Recv(toutletsY, nxy, MPI_INT, MPI_ANY_SOURCE, 5, MCW, &status);
			MPI_Recv(twiddown, nxy, MPI_INT, MPI_ANY_SOURCE, 6, MCW, &status);
			MPI_Recv(tdist_moved, nxy, MPI_LONG, MPI_ANY_SOURCE, 7, MCW, &status);
		}

        	for( i=0; i<nxy; i++){
			//if a node in the temp arrays belongs to us, and we don't already have it, get it
			flowData->globalToLocal(toutletsX[i], toutletsY[i], txn, tyn);
			flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
			if(flowData->isInPartition(txn,tyn) && !(part_has[i]==rank)){
				outletsX[i]=toutletsX[i];
				outletsY[i]=toutletsY[i];
				dist_moved[i]=tdist_moved[i];
				widdown[i]=twiddown[i];
			}
			
		}
        delete [] toutletsX;
        delete [] toutletsY;
		delete [] tdist_moved;
		delete [] twiddown;

		// each process figures out how many nodes it currently has and how many are done
		localnodes = 0;
		localdone = 0;
        	for( i=0; i<nxy; i++){
			flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
			if(flowData->isInPartition(tx,ty)){
				localnodes++;
				if(dist_moved[i]<0)localdone++;  // DGT added this as a termination condition
			}
		}

		//total up all the nodes and all the finished nodes
		MPI_Reduce(&localnodes, &totalnodes, 1, MPI_INT, MPI_SUM, 0, MCW);
		MPI_Reduce(&localdone, &totaldone, 1, MPI_INT, MPI_SUM, 0, MCW);
		//if(!rank)printf("dist = %d\tfinished %d out of %d nodes.\n",dist, totaldone,totalnodes);

		//if all the nodes are done or we've moved them all maxdist, terminate the loop
		if(rank==0){
			if(totaldone==totalnodes){  
				done=1;
			}
		
		}
		MPI_Bcast(&done,1,MPI_INT,0,MCW);
	}

	// look for unresolved outlets, set dist_moved to -1
        for( i=0; i<nxy; i++){
		flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
		if(flowData->isInPartition(tx,ty)){
//			td=srcData->getData(tx,ty,td);
			if(dist_moved[i]==movedist){
				dist_moved[i]=-1;
//				ismoved[i]=0;
			}
		}
	}

	// oooooooooooooooooooooo  end  processing

	// write the shapefile that contains the moved src points

	double *tempxnode, *tempynode;
	long  *tempdist_moved;  //  *tempismoved,
	tempxnode = new double[nxy];
	tempynode = new double[nxy];
//	tempismoved = new long[nxy];
	tempdist_moved = new long[nxy];
	int *twiddown = new int[nxy];

	for(i=0;i<nxy;++i){
		tempxnode[i]=0;
		tempynode[i]=0;
//		tempismoved[i]=0;
		tempdist_moved[i]=0;
		twiddown[i]=0;
	}

	//if(!rank)printf("setting up temp arrays...",dist, totaldone,totalnodes);
	// set temp arrays with local data in each process
	// each local array should have it's local data in the array at the proper place
	// zeros elsewhere
        for( i=0; i<nxy; i++){
        	p.globalXYToGeo(outletsX[i], outletsY[i], xnode[i], ynode[i]);
		flowData->globalToLocal(outletsX[i], outletsY[i], tx, ty);
		if(flowData->isInPartition(tx,ty)){
			tempxnode[i]=xnode[i];
			tempynode[i]=ynode[i];
//			tempismoved[i]=ismoved[i];
			tempdist_moved[i]=dist_moved[i];
			twiddown[i]=widdown[i];
/*
			printf("p%d:\t",rank);
			printf("x:%d\t",outletsX[i]);
			printf("y:%d\t",outletsY[i]);
			printf("strm:%d\t",td);
			printf("dir:%d\t",tf);
			printf("distmvd:%d\t",dist_moved[i]);
			printf("ismoved:%d\t",ismoved[i]);
			printf("\n");
*/
		}
	}
	if(!rank){
        	for( i=0; i<nxy; i++){
			// if the outlet is outside the DEM, p0 puts it back in the temp array
			if(outletsX[i]<0||outletsX[i]>=pTotalX||outletsY[i]<0||outletsY[i]>=pTotalY){
				tempxnode[i]=origxnode[i];
				tempynode[i]=origynode[i];
	//			tempismoved[i]=0;
				tempdist_moved[i]=-1;
				twiddown[i]=-1;
			} 
			// if dist_moved == 0, use original points
		}
	}
	//if(!rank)printf("done.\n",dist, totaldone,totalnodes);


	//if(!rank)printf("reducing data...",dist, totaldone,totalnodes);
	MPI_Reduce(tempxnode, xnode, nxy, MPI_DOUBLE, MPI_SUM, 0, MCW);
	MPI_Reduce(tempynode, ynode, nxy, MPI_DOUBLE, MPI_SUM, 0, MCW);
//	MPI_Reduce(tempismoved, ismoved, nxy, MPI_LONG, MPI_SUM, 0, MCW);
	MPI_Reduce(tempdist_moved, dist_moved, nxy, MPI_LONG, MPI_SUM, 0, MCW);
	MPI_Reduce(twiddown, widdown, nxy, MPI_INT, MPI_SUM, 0, MCW);
	//if(!rank)printf("done\n.",dist, totaldone,totalnodes);

	//if(!rank){
 //       	for( i=0; i<nxy; i++){
	//		// if the distance moved is 0, use original x y
	//		if(dist_moved[i]<=0){  // DGT changed condition to dist_moved <=0 because original values kept whenever not moved for whatever reason
	//			xnode[i]=origxnode[i];
	//			ynode[i]=origynode[i];
	//		} 
	//	}
	//}

	delete [] tempxnode;
	delete [] tempynode;
//	delete [] tempismoved;
	delete [] tempdist_moved;
	delete [] twiddown;
	OGRRegisterAll();
	//if(!rank)printf("inserting shapes...",dist, totaldone,totalnodes);
	//if(rank==0)printf("--\n");
	if(rank==0){
		//  write a shape file
		//SHPHandle sh;
		//DBFHandle dbf;
		//sh = SHPCreate(outletshapefile, SHPT_POINT);
		//char outletsdbf[MAXLN];
		//nameadd(outletsdbf, outletshapefile, ".dbf");
		//dbf = DBFCreate(outletsdbf);
		int nfields;
		nfields=2; // CWG looks like this should be 3, but not used anyway
		//int idIndx = DBFAddField(dbf, "id", FTInteger, 6, 0);
		//int iddownIndx = DBFAddField(dbf, "id_down", FTInteger, 6, 0);
		//int ad8Indx = DBFAddField(dbf, "ad8", FTDouble, 12, 0);
		

      const char *pszDriverName = "ESRI Shapefile";
      driver = OGRGetDriverByName( pszDriverName );
      if( driver == NULL )
      {
         printf( "%s driver not available.\n", pszDriverName );
         exit( 1 );
      }

    /* Create new file using this driver */
     hDSsh = OGR_Dr_CreateDataSource(driver, outletshapefile, NULL);
     if (hDSsh == NULL)
      {
        printf("Unable to create %s\n", outletshapefile);
        exit( 1 );
	 }

     char *layername; 
     layername=getLayername(outletshapefile); // get layer name
    //hLayer1 = OGR_DS_GetLayerByName( hDS1,layername );
    //OGR_L_ResetReading(hLayer1);
	
     hLayersh= OGR_DS_CreateLayer( hDSsh, layername ,hSRSRaster, wkbPoint, NULL );
     if( layername  == NULL )
     {
        printf( "Layer creation failed.\n" );
        exit( 1 );
     }
 
	
	
    /* Add a few fields to the layer defn */
    hFieldDefnsh = OGR_Fld_Create( "id", OFTInteger );
    OGR_L_CreateField(hLayersh,  hFieldDefnsh, 0);
   hFieldDefnsh= OGR_Fld_Create( "id_down", OFTInteger );
    OGR_L_CreateField(hLayersh,  hFieldDefnsh, 0);
	hFieldDefnsh = OGR_Fld_Create( "ad8", OFTReal );
    OGR_L_CreateField(hLayersh,  hFieldDefnsh, 0);

   for(int i=0; i<nxy; i++)
		{

			
		double x = origxnode[i];  // DGT says does not need +pdx/2.0;
		double y = origynode[i];  // DGT +pdy/2.0;
        hFeaturesh = OGR_F_Create( OGR_L_GetLayerDefn( hLayersh ) );
		// There is a possibility that these id's could be no data values.  In this case just do not write them
		if(wid[i] !=  nodatav){
			OGR_F_SetFieldInteger( hFeaturesh, OGR_F_GetFieldIndex(hFeaturesh, "id"), wid[i] );}
		if(widdown[i] != nodatav){
			OGR_F_SetFieldInteger( hFeaturesh, OGR_F_GetFieldIndex(hFeaturesh, "id_down"), widdown[i] );}
		OGR_F_SetFieldDouble( hFeaturesh, OGR_F_GetFieldIndex(hFeaturesh, "ad8"), (double)ad8max[i] );

        hGeometrysh = OGR_G_CreateGeometry(wkbPoint);
        OGR_G_SetPoint_2D(hGeometrysh, 0, x, y);
        OGR_F_SetGeometry( hFeaturesh, hGeometrysh ); 
		OGR_G_DestroyGeometry(hGeometrysh);
        if( OGR_L_CreateFeature( hLayersh, hFeaturesh) != OGRERR_NONE )
            {
              printf( "Failed to create feature in shapefile.\n" );
              exit( 1 );
            }
   
			OGR_F_Destroy( hFeaturesh );


			// CWG should check res is not 0
		}
		
      OGR_DS_Destroy( hDSsh );
     hDSshmoved = OGR_Dr_CreateDataSource(driver, movedoutletshapefile, NULL);
     if (hDSshmoved == NULL)
      {
        printf("Unable to create %s\n", movedoutletshapefile);
        exit( 1 );
	 }

     char *layernamemoved;
	// extract leyer information from shapefile
    layernamemoved=getLayername(movedoutletshapefile);
    //hLayer1 = OGR_DS_GetLayerByName( hDS1,layername );
    //OGR_L_ResetReading(hLayer1);
	
	
	hLayershmoved= OGR_DS_CreateLayer( hDSshmoved, layernamemoved,hSRSRaster, wkbPoint, NULL );
    if(  layernamemoved == NULL )
    {
        printf( "Layer creation failed.\n" );
        exit( 1 );
    }
 
	
	
    /* Add a few fields to the layer defn */
    hFieldDefnshmoved = OGR_Fld_Create( "id", OFTInteger );
    OGR_L_CreateField(hLayershmoved,  hFieldDefnshmoved, 0);
    hFieldDefnshmoved= OGR_Fld_Create( "id_down", OFTInteger );
    OGR_L_CreateField(hLayershmoved,  hFieldDefnshmoved, 0);
	hFieldDefnshmoved = OGR_Fld_Create( "ad8", OFTReal );
    OGR_L_CreateField(hLayershmoved,  hFieldDefnshmoved, 0);
    for(i=0;i<nxy;++i){
		
			//hFeatureshmoved=OGR_L_GetFeature(hLayershmoved,i);
			double x = xnode[i];  // DGT says does not need +pdx/2.0;
			double y = ynode[i];  // DGT +pdy/2.0;
		
			
		

        hFeatureshmoved = OGR_F_Create( OGR_L_GetLayerDefn( hLayershmoved ) );
        if(wid[i] !=  nodatav){
			OGR_F_SetFieldInteger( hFeatureshmoved, OGR_F_GetFieldIndex(hFeatureshmoved, "id"), wid[i] );}
		if(widdown[i] !=  nodatav){
			OGR_F_SetFieldInteger( hFeatureshmoved, OGR_F_GetFieldIndex(hFeatureshmoved, "id_down"), widdown[i] );}
		OGR_F_SetFieldDouble( hFeatureshmoved, OGR_F_GetFieldIndex(hFeatureshmoved, "ad8"), (double)ad8max[i] );

       
        hGeometryshmoved = OGR_G_CreateGeometry(wkbPoint);
        OGR_G_SetPoint_2D(hGeometryshmoved, 0, x, y);
        OGR_F_SetGeometry( hFeatureshmoved, hGeometryshmoved ); 
		OGR_G_DestroyGeometry(hGeometryshmoved);
        if( OGR_L_CreateFeature( hLayershmoved, hFeatureshmoved) != OGRERR_NONE )
            {
              printf( "Failed to create feature in shapefile.\n" );
              exit( 1 );
            }
   
	   OGR_F_Destroy( hFeatureshmoved );


			// CWG should check res is not 0
		}
	 OGR_DS_Destroy( hDSshmoved );
	}
	//if(!rank)printf("done\n.",dist, totaldone,totalnodes);
	

	delete [] xnode;
	delete [] ynode;
//	delete [] ismoved;
	delete [] dist_moved;
    delete [] outletsX;
    delete [] outletsY;
	delete [] part_has;






	end = MPI_Wtime();
	double total,temp;
        total = end-begin;
        MPI_Allreduce (&total, &temp, 1, MPI_DOUBLE, MPI_SUM, MCW);
        total = temp/size;

	
	if( rank == 0) 
		printf("Total time: %f\n",total);

	}MPI_Finalize();


	return 0;
}


