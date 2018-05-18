/*
clonex.c

Version : 0.0.02
Author  : Niko Beerenwinkel
    Moritz Gerstung

(c) Niko Beerenwinkel, Moritz Gerstung 2009
 */


#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <math.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_sf_gamma.h>
#include <getopt.h>
#include <omp.h>


#define d 1000000 // dimension (number of loci)
#define MAX_GENOTYPES 1000000  // max. 10^6
#define MAX_K 1000  // max. no. of mutations per genotype


typedef int (*compfunc) (const void*, const void*);  // type for comparison function in qsort


struct Genotype
{
	int mutation[MAX_K+1];
	int k;  // number of mutations
	int count;
};


struct Genotype genotype[MAX_GENOTYPES];
double geno_prob[MAX_GENOTYPES];
unsigned int geno_count[MAX_GENOTYPES];
int N_g;  // current number of genotypes (make sure that N_g <= MAX_GENOTYPES)
double fitness[MAX_K+1];

int sum_obs[d];
int k_abs_freq[MAX_K+1];
double k_rel_freq[MAX_K+1];

gsl_rng *RNG;  // random number generator


int hamming_distance(i1, i2)
{
	int j, l, g1[d], g2[d];

	for (j=0; j<d; j++)
		g1[j] = g2[j] = 0;

	for (l=0; l<genotype[i1].k; l++)
		g1[genotype[i1].mutation[l]] = 1;
	for (l=0; l<genotype[i2].k; l++)
		g1[genotype[i2].mutation[l]] = 1;

	int dist = 0;
	for (j=0; j<d; j++)
		dist += (g1[j] != g2[j]);

	return dist;
}


double expected_hamming_distance()
{
	int i, i1, i2;

	double rel_freq[MAX_GENOTYPES];
	int N = 0;
	for (i=0; i<N_g; i++)
		N += genotype[i].count;
	for (i=0; i<N_g; i++)
		rel_freq[i] = (double) genotype[i].count / (double) N;

	double dist = 0.0;
	for (i1=0; i1<N_g; i1++)
		for (i2=i1+1; i2<N_g; i2++)
			dist += rel_freq[i1] * rel_freq[i2] * (double) hamming_distance(i1, i2);

	return dist;
}



double detect_mutations(int *k_min, int *k_max, double *k_mean, int *k_median, int *k_obs, double *k_plus, double *k_minus, double *prob_add, double *homo, double *div, double delta)
{
	int i, j, l;
	int k, count;

	for (j=0; j<d; j++)
		sum_obs[j] = 0;


	for (k=0; k<=MAX_K; k++)
	{
		k_abs_freq[k] = 0;
		k_rel_freq[k] = 0.0;
	}

	//#pragma omp parallel for private(k,count)
	for (i=0; i<N_g; i++)
	{
		k = genotype[i].k;
		count = genotype[i].count;

		k_abs_freq[k] += count;
		for (l=0; l<k; l++)
			sum_obs[genotype[i].mutation[l]] += count;
	}

	int N = 0;
	int max_freq = 0;
	for (k=0; k<=MAX_K; k++)
	{
		N += k_abs_freq[k];
		if ((k == 0) || (k_abs_freq[k] > max_freq))
		{
			max_freq = k_abs_freq[k];
			*k_median = k;
		}
	}


	int k_median_count = 0;
	int dominant_count = 0;
	int dominant_type;
	for (i=0; i<N_g; i++)
	{
		if (genotype[i].k == *k_median)
		{
			k_median_count += genotype[i].count;
			if ((dominant_count == 0) || (genotype[i].count > dominant_count))
			{
				dominant_type = i;
				dominant_count = genotype[i].count;
			}
		}
	}
	*homo = (double) dominant_count / (double) k_median_count;

	*div = expected_hamming_distance();

	for (k=0; k<=MAX_K; k++)
		k_rel_freq[k] = (double) k_abs_freq[k] / (double) N;

	*k_min = 0;
	while (k_abs_freq[*k_min] == 0)
		(*k_min)++;

	*k_max = MAX_K;
	while (k_abs_freq[*k_max] == 0)
		(*k_max)--;

	*k_mean = 0.0;
	for (k=0; k<=MAX_K; k++)
		*k_mean += k_rel_freq[k] * (double) k;

	int g_obs[d], g[d];
	for (j=0; j<d; j++)
		g_obs[j] = 0;

	*k_obs = 0;
	for (j=0; j<d; j++)
		if (sum_obs[j] > delta * (double) N)
		{
			(*k_obs)++;
			g_obs[j] = 1;
		}

	double E_dist = 0.0;
	*k_plus = *k_minus = 0.0;
	*prob_add = 0.0;

	double k_pp, k_mm, prob_aa;
	k_pp = k_mm = prob_aa = 0.0;

	//#pragma omp parallel for reduction(+: E_dist, k_pp, k_mm, prob_aa)
	for (i=0; i<N_g; i++)
	{
		for (j=0; j<d; j++)
			g[j] = 0;

		for (l=0; l<genotype[i].k; l++)
			g[genotype[i].mutation[l]] = 1;

		int dist = 0;
		int plus = 0;
		int minus = 0;
		for (j=0; j<d; j++)
		{
			dist += (g[j] != g_obs[j]);
			plus += (g[j] > g_obs[j]);  // mutation in cell i that is not observable in population
			minus += (g[j] < g_obs[j]);  // mutation observable, but not present in cell i
		}

		double frac = (double) genotype[i].count / (double) N;
		E_dist += frac * (double) dist;
		k_pp += frac * (double) plus;
		k_mm += frac * (double) minus;
		if (plus > 0)
			prob_aa += frac;
	}

	*k_plus = k_pp;
	*k_minus = k_mm;
	*prob_add = prob_aa;


	/*
double p = 0.0;
for (k=(*k_obs)+1; k<=*k_max; k++)
p += k_rel_freq[k];
	 */

	return E_dist;
}



void summary_to_R(FILE *DB)
{
	int i, k;
    fprintf(DB, "structure(list(n=c(");
	for (i=0; i<N_g; i++)
	{
		fprintf(DB, "%d", genotype[i].count);
		if(i<N_g-1)
			fprintf(DB, ", ");
	}
	fprintf(DB, "), genotype=c(");
	for (i=0; i<N_g; i++)
	{
		fprintf(DB, "\"");
		for (k=0; k<genotype[i].k; k++)
		{
			fprintf(DB, "%d", genotype[i].mutation[k]);
			if(k<genotype[i].k-1)
				fprintf(DB, ":");
		}
		fprintf(DB, "\"");
		if(i<N_g-1)
			fprintf(DB, ", ");
	}
	fprintf(DB, ")), row.names=c(NA, %i), class=\"data.frame\")", N_g);
}

void summary_to_tsv(FILE *DB, int gen)
{
	int i, k;
    //fprintf(DB, "n\tgenotype\n");
	for (i=0; i<N_g; i++)
	{
		fprintf(DB, "%d\t%d\t", gen, genotype[i].count);
		for (k=0; k<genotype[i].k; k++)
		{
			fprintf(DB, "%d", genotype[i].mutation[k]);
			if(k<genotype[i].k-1)
				fprintf(DB, ":");
		}
		fprintf(DB, "\n");
	}
}

int count_cmp (const struct Genotype *g, const struct Genotype *h)
{
	return (h->count - g->count);  // decreasing
}


int no_of_mut_cmp (const struct Genotype *g, const struct Genotype *h)
{
	return (h->k - g->k);  // decreasing
}


int pos_zero_cmp (const struct Genotype *g, const struct Genotype *h)
{
	if ((h->count > 0) && (g->count == 0))
		return 1;
	if ((h->count == 0) && (g->count > 0))
		return -1;

	return 0;
}


void remove_zeros(int total_sort)
{

	if (!total_sort) //normally (0) only sort out zeros
		qsort(genotype, N_g, sizeof(struct Genotype), (compfunc) pos_zero_cmp);
	else
		qsort(genotype, N_g, sizeof(struct Genotype), (compfunc) count_cmp);

	while (genotype[N_g-1].count == 0)
		N_g--;

}

void remove_zeros_fast()
{
	int i;
	for(i=0; i<N_g;i++){
		if(genotype[i].count==0){
			while(genotype[N_g-1].count==0) N_g--;
			genotype[i] = genotype[N_g-1];
			N_g--;
		}
	}
}


int int_cmp (const int *a, const int *b)
{
	return (*a - *b);  // increasing
}


int mutation_cmp (const struct Genotype *g, const struct Genotype *h)
{
	int j;

	if (h->k != g->k)
		return (h->k - g->k);  // decreasing
	else
	{
		for (j=0; j<g->k; j++)
		{
			if (g->mutation[j] < h->mutation[j])
				return -1;
			if (g->mutation[j] > h->mutation[j])
				return 1;
		}
		return 0;
	}


}


void remove_duplicates()
{
	int i;
	//#pragma omp parallel for
	for (i=0; i<N_g; i++)
		qsort(genotype[i].mutation, genotype[i].k, sizeof(int), (compfunc) int_cmp);  // sort mutation lists

	int split = floor((double)N_g/2);

	#pragma omp parallel num_threads(2)
	{
	#pragma omp task
		{qsort(genotype, split, sizeof(struct Genotype), (compfunc) mutation_cmp);}  // sort population
	#pragma omp task
		{qsort(genotype + split, N_g, sizeof(struct Genotype), (compfunc) mutation_cmp);}  // sort population}
	}
	qsort(genotype, N_g, sizeof(struct Genotype), (compfunc) mutation_cmp);  // sort population


	for (i=0; i<N_g-1; i++)
	{
		if (mutation_cmp(&genotype[i], &genotype[i+1]) == 0)  // collect counts
		{
			genotype[i+1].count += genotype[i].count;
			genotype[i].count = 0;
		}
	}

	remove_zeros_fast();


}



int simulate(FILE* DB, int N_init, int N_fin, int gen_max, double u, double v, double s, double s1, int run, int d0, int d1, int verbose)
{

	int gen, i, j, c, N; //k;
	int mutation, index_mut; //, mutation_number, free_slots;
	int mut[MAX_K + 1], mut_k;

	double p, N_exp_growth;
	//double v = u;
	//double k_mean, p_more, k_plus, k_minus, prob_add, homo, div;
	//int k_min, k_max, k_median;
	//int k_obs;

	double a = exp( ( log(N_fin) - log(N_init) ) / gen_max );  // exponential growth rate
	/* ensures growth in gen_max generations from N_init to N_fin */

	//a = exp( log(2) / 60 );  // doubling time is 60 generations
	//a = exp( log(2) / 30 );  // doubling time is 30 generations

	printf("a = %f\n", a);
	printf("doubling time = %f generations\n", log(2) / log(a));


	for (j=0; j<MAX_K; j++)
		fitness[j] = pow(1.0 + s, (double) j);


	// initialize:
	N = N_init;
	N_exp_growth = (double) N;

	for (j=0; j<MAX_K; j++)
		genotype[0].mutation[j] = 0;
	genotype[0].k = 0;
	genotype[0].count = N;
	N_g = 1;

	// grow population:
	gen = 0;
	do
	{
		gen++;

		/* population growth */

		N_exp_growth *= a;
		N = (int) (N_exp_growth + 0.5);

		if (N > 2000000000)
		{
			printf("Polyp has grown too large!\n");
			exit(1);
		}


		/* selection */
		double fit = 1.0;

		// compute probabilities:
		for (i=0; i<N_g; i++)
		{
			for (j=0;j<genotype[i].k;j++)
			{
				if (genotype[i].mutation[j] > 0 && genotype[i].mutation[j] <= d - d0)
				{  
					if (genotype[i].mutation[j] <= d - d0 - d1 )
						fit *= 1 + s;
					else
						fit *= 1 + s1;
				}
				//      printf("%f\t%i\n", fit, genotype[i].k);
			}
			//geno_prob[i] = fitness[genotype[i].k] * genotype[i].count;  // no need to normalize for gsl function
			geno_prob[i] = fit * genotype[i].count;  // no need to normalize for gsl function
			fit = 1.0; // reset fitness to 1.0 for next genotype.
		}


		for (i=0; i<N_g; i++)
			geno_count[i] = genotype[i].count;

		gsl_ran_multinomial(RNG, N_g, N, geno_prob, geno_count);

		for (i=0; i<N_g; i++)
			genotype[i].count = geno_count[i];

		remove_zeros_fast();  // because low-frequency mutants are likely not to be sampled
		// and we need to consider less genotypes for mutation


		/* mutation */

		double mu;
		int l,m;
		double a, b;

		for(m=0; m<2; m++){
			if(m == 0){ // driver
				mu = u;
				l = d-d0;
				a = 1.0;
				b = (double)d - (double)d0 + 1.0;

			}else{ // passenger mutation
				mu = v;
				l = d0;
				a = (double)d - (double)d0 + 1.0;
				b = (double)d + 1.0;

			}

			int N_mut_cells;

			//#pragma omp parallel for private(p, N_mut_cells,i,c,index_mut,mutation, mut, mut_k,j,)
			for (i=0; i<N_g; i++)
			{
				p = 1.0 - gsl_ran_binomial_pdf(0, mu, l);  // prob. of >= 1 mutation
				N_mut_cells = gsl_ran_binomial(RNG, p, genotype[i].count);  // number of mutated cells

				if (N_g + N_mut_cells > MAX_GENOTYPES)
				{
					printf("Too many genotypes: out of memory\n");
					printf("MAX_GENOTYPES = %d\n", MAX_GENOTYPES);
					summary_to_tsv(DB, gen);
					exit(1);
				}

				//printf("%i\n", N_g);
				for (c=0; c<N_mut_cells; c++)
				{
					// make a copy of genotype i for mutation, decrease count:
					(genotype[i].count)--;
					if (genotype[i].count == 0)
						index_mut = i;  // overwrite
					else
					{
						index_mut = N_g;
						genotype[index_mut] = genotype[i];  // copy
						N_g++;
					}
					genotype[index_mut].count = 1;


					if (genotype[index_mut].k - 2 > MAX_K)
					{
						printf("Warning: More than %d mutations generated!\n", MAX_K);
						genotype[index_mut].k = MAX_K - 2;  // reset
					}

					// add first mutation:
					mutation = (int) floor(gsl_ran_flat(RNG, a, b));


					//printf("%i\n", mutation);

					genotype[index_mut].mutation[genotype[index_mut].k] = mutation;
					(genotype[index_mut].k)++;


					// maybe add a second mutation:
					p = gsl_ran_binomial_pdf(0, mu, l);

					// prob of zero additional mutations
					if (gsl_ran_flat(RNG, 0.0, 1.0) > p)  // i.e., additional mutations, assume exactly 1
					{
						mutation = (int) floor(gsl_ran_flat(RNG, a, b));
						//printf("%i\n", mutation);
						genotype[index_mut].mutation[genotype[index_mut].k] = mutation;
						(genotype[index_mut].k)++;
					}

					// remove duplicate mutations (mutation[] is just a list!):
					if (genotype[index_mut].k > 1)
					{
						qsort(genotype[index_mut].mutation, genotype[index_mut].k, sizeof(int), (compfunc) int_cmp);
						mut[0] = genotype[index_mut].mutation[0];
						mut_k = 1;
						for (j=1; j<genotype[index_mut].k; j++)
						{
							if (genotype[index_mut].mutation[j] != genotype[index_mut].mutation[j-1])
							{
								mut[mut_k] = genotype[index_mut].mutation[j];
								mut_k++;
							}
						}
						// overwrite:
						for (j=0; j<mut_k; j++)
							genotype[index_mut].mutation[j] = mut[j];
						genotype[index_mut].k = mut_k;

					}


				}

			}
		}


		//p_more = detect_mutations(&k_min, &k_max, &k_mean, &k_obs, 0.5);

		remove_zeros_fast();

		//fprintf(DB, "%d\t%d\t%d\t%f\t%d\t%d\t%f\n", gen, N, k_min, k_mean, k_max, k_obs, p_more);


		if (gen % 10 == 0)
		{
			remove_duplicates();
			summary_to_tsv(DB, gen);
			printf(".");
			fflush(stdout);

			//if(gen < gen_max)
			//fprintf(DB, ", ");
		}
		if (gen == gen_max)
			printf("\n");
		//		{
		//			remove_duplicates();
		//			remove_zeros(1);
		//			summary(DB);

		//for (j=0; j<d; j++)
		//printf("%d\n", sum_obs[j]);

		//p_more = detect_mutations(&k_min, &k_max, &k_mean, &k_median, &k_obs, &k_plus, &k_minus, &prob_add, &homo, &div, 0.5);
		//fprintf(DB, "%d\t%d\t%d\t%d\t%g\t%g\t%d\t%g\t%d\t%d\t%d\t%g\t%g\t%g\t%g\t%g\t%g\n", run, gen, N_init, N, u, s, k_min, k_mean, k_median, k_max, k_obs, p_more, k_plus, k_minus, prob_add, homo, div);


		//}


	}
	while(gen < gen_max);



	return 0;
}



void print_pop(FILE* DB, int N, double s, double s_super, int d0)
{
	int i, j, k, locus[d], anylocus[d];
	double fit;
	for (j=0; j<d; j++)
		anylocus[j] = 0;
	for (i=0; i<N_g; i++)
		for (k=0; k< genotype[i].k; k++)
			anylocus[genotype[i].mutation[k] - 1]++;

	for (i=0; i<N_g; i++)
	{
		for (j=0; j<d; j++)
			locus[j] = 0;
		for (k=0; k< genotype[i].k; k++){
			locus[genotype[i].mutation[k] - 1] = 1;
		}
		fit = 1.0;

		for (j=0;j<genotype[i].k;j++)
		{
			if (genotype[i].mutation[j] > 0 && genotype[i].mutation[j] <= d)
			{  
				if (genotype[i].mutation[j]>5 )
					fit *= 1 + s;
				else
					fit *= 1 + s_super;
			}
		}


		fprintf(DB, "%g\t", (double)genotype[i].count/(double)N);
		fprintf(DB, "%d\t", genotype[i].k);
		fprintf(DB, "%f\t", fit);
		for (j=0; j<d; j++)
		{
			if(j==5 || j==d-d0)
				fprintf(DB,"\t");
			//if(anylocus[j]>0)
			fprintf(DB, "%d", locus[j]);
		}
		fprintf(DB, "\n");

	}	
}





int main(int argc, char **argv)
{

	// defaults:
	int    N = 1000000000;  // population size
	int N_init = 1;
	//  int    n_d = 100;  // drivers
	//  int    n_p = 100;  // passengers
	double u = 1e-7;  // mutation rate
	double v = -1.0; //mutation rate (passengers)
	double s = 1e-2;  // Selective advantage
	double s1 = 1.5*s; // Selective advantage of super drivers
	int    g = 1800;  // number of Generations
	int    R = 1;  // number of simulations Runs
	int    d0 = 0; //number of passengers
	int    d1 = 0; //number of super drivers
	char *filestem;
	unsigned int seed = (unsigned) time(NULL);  // r, random seed
	int verbose = 0;

	int error_flag = 0;
	int f_flag = 0;

	int c = 0;
	while((c = getopt(argc, argv, "N:n:u:v:s:t:g:R:f:r:p:dh")) != EOF )
	{
		switch(c)
		{
		case 'N':
			if (atoi(optarg) > 0)
				N = atoi(optarg);
			else
				error_flag++;
			break;

		case 'n':
			if (atoi(optarg) > 0)
				N_init = atoi(optarg);
			else
				error_flag++;
			break;

		case 'p':
			if (atoi(optarg) > 0)
				d0 = atoi(optarg);
			else
				error_flag++;
			break;

		case 'q':
			if (atoi(optarg) > 0)
				d1 = atoi(optarg);
			else
				error_flag++;
			break;

		case 'u':
			if (atof(optarg) > 0)
				u = atof(optarg);
			else
				error_flag++;
			break;

		case 'v':
			if (atof(optarg) > 0)
				v = atof(optarg);
			else
				error_flag++;
			break;

		if(v == -1.0) v=u;

		case 's':
			if (atof(optarg) >= 0)
				s = atof(optarg);
			else
				error_flag++;
			break;

		case 't':
			if (atof(optarg) >= 0)
				s1 = atof(optarg);
			else
				error_flag++;
			break;

		case 'g':
			if (atoi(optarg) > 0)
				g = atoi(optarg);
			else
				error_flag++;
			break;

		case 'R':
			if (atoi(optarg) >= 0)
				R = atoi(optarg);
			else
				error_flag++;
			break;

		case 'f':
			filestem = optarg;
			f_flag++;
			break;

		case 'r':
			seed = atoi(optarg);
			break;

		case 'd':
			verbose = 1;
			break;

		case 'h':
			printf("usage: clonex [-NusgRrh]\n");
			printf("  N - Final population size (default = %d)\n", N);
			printf("  n - Initial population size (default = %d)\n", N_init);
			printf("  u - Mutation rate (default = %g)\n", u);
			printf("  v - Mutation rate passengers (default = %g)\n", v);
			printf("  s - Selective advantage (default = %g)\n", s);
			printf("  t - Selective advantage of other drivers (default = %g)\n", s1);
			printf("  g - Number of generations (default = %d)\n", g);
			printf("  p - Number of passengers (default = %d)\n", d0);
			printf("  q - Number of other drivers (default = %d)\n", d1)
			printf("  R - Replicates (default = %d)\n", R);
			printf("  r - Random seed (default = time)\n");
			printf("  f - File directory (Required! Make sure that the directory exists!)\n");
			printf("  h - This help\n");
			exit(0);

		default :
			exit(1);
		}
	}

	if( d0 + d1 > d) error_flag++;

	if (error_flag || (! f_flag))
	{
		fprintf(stderr, "Error!\n");
		exit(1);
	}

	// random number generator:
	RNG = gsl_rng_alloc (gsl_rng_taus);  // global variable
	gsl_rng_set(RNG, seed);  // seed rng
	if (gsl_rng_max(RNG) - gsl_rng_min(RNG) < N)
	{
		printf("Population size N = %d too large for random number generator!\n", N);
		printf("RNG range = [%lu, %lu]\n", gsl_rng_min(RNG), gsl_rng_max(RNG));
		exit(1);
	}


	char summary_filename[255], filename[255];



	int r;
	//#pragma omp parallel for private(DB, genotype, geno_prob, geno_count, N_g, fitness, sum_obs, k_abs_freq, k_rel_freq)
	for (r=0; r<R; r++)
	{
		printf("Sample %i/%i\n", r+1,R);
		sprintf(summary_filename, "%s/r%03d.R", filestem, r+1);
		FILE *DB;
		if ((DB = fopen(summary_filename, "w")) == NULL)
		{
			fprintf(stderr, "Cannot open output file -- %s\n", summary_filename);
			exit(1);
		}
		//fprintf(DB, "pop <- list(");
		simulate(DB, N_init, N, g, u, v, s, s1, r+1, d0, d1, verbose);
		//fprintf(DB, ")");

		fclose(DB);

		//printf("Succeed.\n");
		//sprintf(filename, "%s/r%03d.pop", filestem, r+1);
		//if ((DB = fopen(filename, "w")) == NULL)
		//{
		//	fprintf(stderr, "In run %d: Cannot open output file -- %s\n", r+1, filename);
		//	exit(1);
		//}
		//print_pop(DB,N, s, s_super, d0);
		//fclose(DB);
	}



	return 0;
}


