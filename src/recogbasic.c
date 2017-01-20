/*====================================================================*
 -  Copyright (C) 2001 Leptonica.  All rights reserved.
 -
 -  Redistribution and use in source and binary forms, with or without
 -  modification, are permitted provided that the following conditions
 -  are met:
 -  1. Redistributions of source code must retain the above copyright
 -     notice, this list of conditions and the following disclaimer.
 -  2. Redistributions in binary form must reproduce the above
 -     copyright notice, this list of conditions and the following
 -     disclaimer in the documentation and/or other materials
 -     provided with the distribution.
 -
 -  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 -  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 -  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 -  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ANY
 -  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 -  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 -  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 -  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 -  OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 -  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 -  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *====================================================================*/

/*!
 * \file recogbasic.c
 * <pre>
 *
 *      Recog creation, destruction and access
 *         L_RECOG            *recogCreateFromRecog()
 *         L_RECOG            *recogCreateFromPixa()
 *         L_RECOG            *recogCreate()
 *         void                recogDestroy()
 *         l_int32             recogGetCount()
 *         l_int32             recogSetParams()
 *         static l_int32      recogGetCharsetSize()
 *
 *      Character/index lookup
 *         l_int32             recogGetClassIndex()
 *         l_int32             recogStringToIndex()
 *         l_int32             recogGetClassString()
 *         l_int32             l_convertCharstrToInt()
 *
 *      Serialization
 *         L_RECOG            *recogRead()
 *         L_RECOG            *recogReadStream()
 *         L_RECOG            *recogReadMem()
 *         l_int32             recogWrite()
 *         l_int32             recogWriteStream()
 *         l_int32             recogWriteMem()
 *         PIXA               *recogExtractPixa()
 *         static l_int32      recogAddCharstrLabels()
 *         static l_int32      recogAddAllSamples()
 *
 *  The recognizer functionality is split into four files:
 *    recogbasic.c: create, destroy, access, serialize
 *    recogtrain.c: training on labeled and unlabeled data
 *    recogident.c: running the recognizer(s) on input
 *    recogdid.c:   running the recognizer(s) on input using a
 *                  document image decoding (DID) hidden markov model
 *
 *  This is a content-adapted (or book-adapted) recognizer (BAR) application.
 *  The recognizers here are typically assembled from data that has
 *  been labeled by a generic recognition system, such as Tesseract.
 *  The general procedure to create a recognizer (recog) from labeled data is
 *  to add the labeled character bitmaps, either one at a time or
 *  all together from a pixa with labeled pix.
 *
 *  The suggested use for a BAR that consists of labeled templates drawn
 *  from a single source (e.g., a book) is to identify unlabeled samples
 *  by using unscaled character templates in the BAR, picking the
 *  template closest to the unlabeled sample.
 *
 *  Outliers can be removed from a pixa of labeled pix.  This is one of
 *  two methods that use averaged templates (the other is greedy splitting
 *  of characters).  See recogtrain.c for a discussion and the implementation.
 *
 *  A special bootstrap recognizer (BSR) can be used to make a BAR from
 *  unlabeled book data.  This is done by comparing character images
 *  from the book with labeled templates in the BSR, where all images
 *  are scaled to h = 40.  The templates can be either the scanned images
 *  or images consisting of width-normalized strokes derived from
 *  the skeleton of the character bitmaps.
 *
 *  Two BARs of labeled character data, that have been made by
 *  different recognizers, can be joined by extracting a pixa of the
 *  labeled templates from each, joining the two pixa, and then
 *  and regenerating a BAR from the joined set of templates.
 *  If all the labeled character data is from a single source (e.g, a book),
 *  identification can proceed using unscaled templates (either the input
 *  image or width-normalized lines).  But if the labeled data comes from
 *  more than one source, (a "hybrid" recognizer), the templates should
 *  be scaled, and we recommend scaling to a fixed height.
 *
 *  Suppose it is not possible to generate a BAR with a sufficient number
 *  of templates of each class taken from a single source.  In that case,
 *  templates from the BSR itself can be added.  This is the condition
 *  described above, where the labeled templates come from multiple
 *  sources, and it is necessary to do all character matches using
 *  templates that have been scaled to a fixed height (e.g., 40).
 *  Likewise, the samples to be identified using this hybrid recognizer
 *  must be modified in the same way.  See prog/recogtest3.c for an
 *  example of the steps that can be taken in the construction of a BAR
 *  using a BSR.
 *
 *  For training numeric input, an example set of calls that scales
 *  each training input to fixed h and will use the line templates of
 *  width linew for identifying unknown characters is:
 *         L_Recog  *rec = recogCreate(0, h, linew, 128, 1);
 *         for (i = 0; i < n; i++) {  // read in n training digits
 *             Pix *pix = ...
 *             recogTrainLabeled(rec, pix, NULL, text[i], 0, 0);
 *         }
 *         recogTrainingFinished(rec, 1);  // required
 *
 *  It is an error if any function that computes averages, removes
 *  outliers or requests identification of an unlabeled character,
 *  such as:
 *     (1) computing the sample averages: recogAverageSamples()
 *     (2) removing outliers: recogRemoveOutliers()
 *     (3) requesting identification of an unlabeled character:
 *         recogIdentifyPix()
 *  is called before an explicit call to finish training.  Note that
 *  to do further training on a "finished" recognizer, just set
 *         recog->train_done = FALSE;
 *  add the new training samples, and again call
 *         recogTrainingFinished(rec, 1);  // required
 *
 *  If not scaling, using the images directly for identification, and
 *  removing outliers, do something like this:
 *      L_Recog  *rec = recogCreate(0, 0, 0, 128, 1);
 *      for (i = 0; i < n; i++) {  // read in n training characters
 *          Pix *pix = ...
 *          recogTrainLabeled(rec, pix, NULL, text[i], 0, 0);
 *      }
 *      recogTrainingFinished(rec, 1);
 *      // remove outliers
 *      recogRemoveOutliers(rec, 0.7, 0.5, 0);
 *
 *  You can generate a recognizer from a pixa where the text field in
 *  each pix is the character string label for the pix.  For example,
 *  the following recognizer will store unscaled line images:
 *      L_Recog  *rec = recogCreateFromPixa(pixa, 0, 0, linew, 128, 1);
 *  and in use, it is fed unscaled line images to identify.
 *
 *  A special case is a "bootstrap" recognizer (BSR), containing images
 *  that are scaled to a fixed height (we use 40 in these examples),
 *  and use either the scanned bitmap:
 *      L_Recog  *recboot = recogCreateFromPixa(pixa, 0, 40, 0, 128, 1);
 *  or width-normalized lines (use width of 5 here):
 *      L_Recog  *recboot = recogCreateFromPixa(pixa, 0, 40, 5, 128, 1);
 *  
 *  This can be used to train a new book adapted recognizer (BAC), on
 *  unlabeled data from, e.g., a book.  To do this, the following is required:
 *   (1) the input images from the book must be scaled in the same
 *       way as those in the BSR
 *   (2) both the BSR and the input images must be set up to be either
 *       input scanned images or width-normalized lines.
 *
 * </pre>
 */

#include <string.h>
#include "allheaders.h"

static const l_int32  INITIAL_PTR_ARRAYSIZE = 20;  /* n'import quoi */
static const l_int32  MAX_EXAMPLES_IN_CLASS = 256;

    /* Default recog parameters that can be changed */
static const l_int32    DEFAULT_CHARSET_TYPE = L_ARABIC_NUMERALS;
static const l_int32    DEFAULT_MIN_NOPAD = 3;

    /* Static functions */
static l_int32 recogGetCharsetSize(l_int32 type);
static l_int32 recogAddCharstrLabels(L_RECOG *recog);
static l_int32 recogAddAllSamples(L_RECOG *recog, PIXAA *paa, l_int32 debug);


/*------------------------------------------------------------------------*
 *                Recog: initialization and destruction                   *
 *------------------------------------------------------------------------*/
/*!
 * \brief   recogCreateFromRecog()
 *
 * \param[in]    recs source recog with arbitrary input parameters
 * \param[in]    scalew  scale all widths to this; use 0 otherwise
 * \param[in]    scaleh  scale all heights to this; use 0 otherwise
 * \param[in]    linew   width of normalized strokes; use 0 to skip
 * \param[in]    threshold for binarization; typically ~128
 * \param[in]    maxyshift from nominal centroid alignment; typically 0 or 1
 * \return  recd, or NULL on error
 *
 * <pre>
 * Notes:
 *      (1) This is a convenience function that generates a recog using
 *          the unscaled training data in an existing recog.
 *      (2) See recogCreate() for use of %scalew, %scaleh and %linew.
 * </pre>
 */
L_RECOG *
recogCreateFromRecog(L_RECOG     *recs,
                     l_int32      scalew,
                     l_int32      scaleh,
                     l_int32      linew,
                     l_int32      threshold,
                     l_int32      maxyshift)
{
L_RECOG  *recd;
PIXA     *pixa;

    PROCNAME("recogCreateFromRecog");

    if (!recs)
        return (L_RECOG *)ERROR_PTR("recs not defined", procName, NULL);

    pixa = recogExtractPixa(recs);
    recd = recogCreateFromPixa(pixa, scalew, scaleh, linew, threshold,
                               maxyshift);
    pixaDestroy(&pixa);
    return recd;
}


/*!
 * \brief   recogCreateFromPixa()
 *
 * \param[in]    pixa of labeled, 1 bpp images
 * \param[in]    scalew  scale all widths to this; use 0 otherwise
 * \param[in]    scaleh  scale all heights to this; use 0 otherwise
 * \param[in]    linew   width of normalized strokes; use 0 to skip
 * \param[in]    threshold for binarization; typically ~128
 * \param[in]    maxyshift from nominal centroid alignment; typically 0 or 1
 * \return  recog, or NULL on error
 *
 * <pre>
 * Notes:
 *      (1) This is a convenience function for training from labeled data.
 *          The pixa can be read from file.
 *      (2) The pixa should contain the unscaled bitmaps used for training.
 *      (3) See recogCreate() for use of %scalew, %scaleh and %linew.
 *      (4) All examples in the same class (i.e., with the same character
 *          label) should be similar.  They can be made similar by invoking
 *          recogRemoveOutliers() on %pixa before calling this function.
 * </pre>
 */
L_RECOG *
recogCreateFromPixa(PIXA        *pixa,
                    l_int32      scalew,
                    l_int32      scaleh,
                    l_int32      linew,
                    l_int32      threshold,
                    l_int32      maxyshift)
{
char     *text;
l_int32   full, n, i, ntext;
L_RECOG  *recog;
PIX      *pix;

    PROCNAME("recogCreateFromPixa");

    if (!pixa)
        return (L_RECOG *)ERROR_PTR("pixa not defined", procName, NULL);
    if (pixaVerifyDepth(pixa, NULL) != 1)
        return (L_RECOG *)ERROR_PTR("not all pix are 1 bpp", procName, NULL);

    pixaIsFull(pixa, &full, NULL);
    if (!full)
        return (L_RECOG *)ERROR_PTR("not all pix are present", procName, NULL);

    n = pixaGetCount(pixa);
    pixaCountText(pixa, &ntext);
    if (ntext == 0)
        return (L_RECOG *)ERROR_PTR("no pix have text strings", procName, NULL);
    if (ntext < n)
        L_ERROR("%d text strings < %d pix\n", procName, ntext, n);

    recog = recogCreate(scalew, scaleh, linew, threshold, maxyshift);
    if (!recog)
        return (L_RECOG *)ERROR_PTR("recog not made", procName, NULL);
    for (i = 0; i < n; i++) {
        pix = pixaGetPix(pixa, i, L_CLONE);
        text = pixGetText(pix);
        if (!text || strlen(text) == 0) {
            L_ERROR("pix[%d] has no text\n", procName, i);
            pixDestroy(&pix);
            continue;
        }
        recogTrainLabeled(recog, pix, NULL, text, 0, 0);
        pixDestroy(&pix);
    }

    recogTrainingFinished(recog, 1);
    return recog;
}


/*!
 * \brief   recogCreate()
 *
 * \param[in]    scalew  scale all widths to this; use 0 otherwise
 * \param[in]    scaleh  scale all heights to this; use 0 otherwise
 * \param[in]    linew   width of normalized strokes; use 0 to skip
 * \param[in]    threshold for binarization; typically ~128
 * \param[in]    maxyshift from nominal centroid alignment; typically 0 or 1
 * \return  recog, or NULL on error
 *
 * <pre>
 * Notes:
 *      (1) If %scalew == 0 and %scaleh == 0, no scaling is done.
 *          If one of these is 0 and the other is > 0, scaling is isotropic
 *          to the requested size.  We typically do not set both > 0.
 *      (2) Use linew > 0 to convert the templates to images with fixed
 *          width strokes.  linew == 0 skips the conversion.
 *      (3) Scaling is used for finding outliers and for training a
 *          book-adapted recognizer (BAR) from a bootstrap recognizer (BSR).
 *          Scaling the height to a fixed value and scaling the width
 *          accordingly (e.g., %scaleh = 40, %scalew = 0) is recommended.
 *      (4) The storage for most of the arrays is allocated when training
 *          is finished.
 * </pre>
 */
L_RECOG *
recogCreate(l_int32      scalew,
            l_int32      scaleh,
            l_int32      linew,
            l_int32      threshold,
            l_int32      maxyshift)
{
L_RECOG  *recog;

    PROCNAME("recogCreate");

    if (scalew < 0 || scaleh < 0)
        return (L_RECOG *)ERROR_PTR("invalid scalew or scaleh", procName, NULL);
    if (linew > 10)
        return (L_RECOG *)ERROR_PTR("invalid linew > 10", procName, NULL);
    if (threshold < 1 || threshold > 255)
        return (L_RECOG *)ERROR_PTR("invalid threshold", procName, NULL);

    if ((recog = (L_RECOG *)LEPT_CALLOC(1, sizeof(L_RECOG))) == NULL)
        return (L_RECOG *)ERROR_PTR("rec not made", procName, NULL);
    recog->templ_use = L_USE_ALL_TEMPL;  /* default for all but outliers */
    recog->threshold = threshold;
    recog->scalew = scalew;
    recog->scaleh = scaleh;
    recog->linew = linew;
    recog->maxyshift = maxyshift;
    recogSetParams(recog, 1, -1);
    recog->bmf = bmfCreate(NULL, 6);
    recog->bmf_size = 6;
    recog->maxarraysize = MAX_EXAMPLES_IN_CLASS;

        /* Generate the LUTs */
    recog->centtab = makePixelCentroidTab8();
    recog->sumtab = makePixelSumTab8();
    recog->sa_text = sarrayCreate(0);
    recog->dna_tochar = l_dnaCreate(0);

        /* Input default values for min component size for splitting.
         * These are overwritten when pixTrainingFinished() is called. */
    recog->min_splitw = 6;
    recog->min_splith = 6;
    recog->max_splith = 60;

        /* Allocate the paa for the unscaled training bitmaps */
    recog->pixaa_u = pixaaCreate(recog->maxarraysize);

        /* Generate the storage for debugging */
    recog->pixadb_boot = pixaCreate(2);
    recog->pixadb_split = pixaCreate(2);
    return recog;
}


/*!
 * \brief   recogDestroy()
 *
 * \param[in,out]   precog will be set to null before returning
 * \return  void
 */
void
recogDestroy(L_RECOG  **precog)
{
L_RECOG  *recog;

    PROCNAME("recogDestroy");

    if (!precog) {
        L_WARNING("ptr address is null\n", procName);
        return;
    }

    if ((recog = *precog) == NULL) return;

    LEPT_FREE(recog->centtab);
    LEPT_FREE(recog->sumtab);
    sarrayDestroy(&recog->sa_text);
    l_dnaDestroy(&recog->dna_tochar);
    pixaaDestroy(&recog->pixaa_u);
    pixaDestroy(&recog->pixa_u);
    ptaaDestroy(&recog->ptaa_u);
    ptaDestroy(&recog->pta_u);
    numaDestroy(&recog->nasum_u);
    numaaDestroy(&recog->naasum_u);
    pixaaDestroy(&recog->pixaa);
    pixaDestroy(&recog->pixa);
    ptaaDestroy(&recog->ptaa);
    ptaDestroy(&recog->pta);
    numaDestroy(&recog->nasum);
    numaaDestroy(&recog->naasum);
    pixaDestroy(&recog->pixa_tr);
    pixaDestroy(&recog->pixadb_ave);
    pixaDestroy(&recog->pixa_id);
    pixDestroy(&recog->pixdb_ave);
    pixDestroy(&recog->pixdb_range);
    pixaDestroy(&recog->pixadb_boot);
    pixaDestroy(&recog->pixadb_split);
    bmfDestroy(&recog->bmf);
    rchDestroy(&recog->rch);
    rchaDestroy(&recog->rcha);
    recogDestroyDid(recog);
    LEPT_FREE(recog);
    *precog = NULL;
    return;
}


/*!
 * \brief   recogGetCount()
 *
 * \param[in]    recog
 * \return  count of classes in recog; 0 if no recog or on error
 */
l_int32
recogGetCount(L_RECOG  *recog)
{
    PROCNAME("recogGetCount");

    if (!recog)
        return ERROR_INT("recog not defined", procName, 0);
    return recog->setsize;
}


/*!
 * \brief   recogSetParams()
 *
 * \param[in]    recog        to be padded, if necessary
 * \param[in]    type     type of char set; -1 for default; see enum in recog.h
 * \param[in]    min_nopad    min number in a class without padding; -1 default
 * \return       0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) This is called when a recog is created.
 *      (2) Default values allow for some padding.  To disable padding,
 *          set %min_nopad = 0.
 * </pre>
 */
l_int32
recogSetParams(L_RECOG     *recog,
               l_int32      type,
               l_int32      min_nopad)
{

    PROCNAME("recogSetParams");

    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);

    recog->charset_type = (type >= 0) ? type : DEFAULT_CHARSET_TYPE;
    recog->charset_size = recogGetCharsetSize(recog->charset_type);
    recog->min_nopad = (min_nopad >= 0) ? min_nopad : DEFAULT_MIN_NOPAD;
    return 0;
}


/*!
 * \brief   recogGetCharsetSize()
 *
 * \param[in]    type of charset
 * \return  size of charset, or 0 if unknown or on error
 */
static l_int32
recogGetCharsetSize(l_int32  type)
{
    PROCNAME("recogGetCharsetSize");

    switch (type) {
    case L_UNKNOWN:
        return 0;
    case L_ARABIC_NUMERALS:
        return 10;
    case L_LC_ROMAN_NUMERALS:
        return 7;
    case L_UC_ROMAN_NUMERALS:
        return 7;
    case L_LC_ALPHA:
        return 26;
    case L_UC_ALPHA:
        return 26;
    default:
        L_ERROR("invalid charset_type %d\n", procName, type);
        return 0;
    }
    return 0;  /* shouldn't happen */
}


/*------------------------------------------------------------------------*
 *                         Character/index lookup                         *
 *------------------------------------------------------------------------*/
/*!
 * \brief   recogGetClassIndex()
 *
 * \param[in]    recog with LUT's pre-computed
 * \param[in]    val integer value; can be up to 3 bytes for UTF-8
 * \param[in]    text text from which %val was derived; used if not found
 * \param[out]   pindex index into dna_tochar
 * \return  0 if found; 1 if not found and added; 2 on error.
 *
 * <pre>
 * Notes:
 *      (1) This is used during training.  There is one entry in
 *          recog->dna_tochar (integer value, e.g., ascii) and
 *          one in recog->sa_text (e.g, ascii letter in a string)
 *          for each character class.
 *      (2) This searches the dna character array for %val.  If it is
 *          not found, the template represents a character class not
 *          already seen: it increments setsize (the number of character
 *          classes) by 1, and augments both the index (dna_tochar)
 *          and text (sa_text) arrays.
 *      (3) Returns the index in &index, except on error.
 *      (4) Caller must check the function return value.
 * </pre>
 */
l_int32
recogGetClassIndex(L_RECOG  *recog,
                   l_int32   val,
                   char     *text,
                   l_int32  *pindex)
{
l_int32  i, n, ival;

    PROCNAME("recogGetClassIndex");

    if (!pindex)
        return ERROR_INT("&index not defined", procName, 2);
    *pindex = -1;
    if (!recog)
        return ERROR_INT("recog not defined", procName, 2);
    if (!text)
        return ERROR_INT("text not defined", procName, 2);

        /* Search existing characters */
    n = l_dnaGetCount(recog->dna_tochar);
    for (i = 0; i < n; i++) {
        l_dnaGetIValue(recog->dna_tochar, i, &ival);
        if (val == ival) {  /* found */
            *pindex = i;
            return 0;
        }
    }

       /* If not found... */
    l_dnaAddNumber(recog->dna_tochar, val);
    sarrayAddString(recog->sa_text, text, L_COPY);
    recog->setsize++;
    *pindex = n;
    return 1;
}


/*!
 * \brief   recogStringToIndex()
 *
 * \param[in]    recog
 * \param[in]    text text string for some class
 * \param[out]   pindex index for that class; -1 if not found
 * \return  0 if OK, 1 on error not finding the string is an error
 */
l_int32
recogStringToIndex(L_RECOG  *recog,
                   char     *text,
                   l_int32  *pindex)
{
char    *charstr;
l_int32  i, n, diff;

    PROCNAME("recogStringtoIndex");

    if (!pindex)
        return ERROR_INT("&index not defined", procName, 1);
    *pindex = -1;
    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);
    if (!text)
        return ERROR_INT("text not defined", procName, 1);

        /* Search existing characters */
    n = recog->setsize;
    for (i = 0; i < n; i++) {
        recogGetClassString(recog, i, &charstr);
        if (!charstr) {
            L_ERROR("string not found for index %d\n", procName, i);
            continue;
        }
        diff = strcmp(text, charstr);
        LEPT_FREE(charstr);
        if (diff) continue;
        *pindex = i;
        return 0;
    }

    return 1;  /* not found */
}


/*!
 * \brief   recogGetClassString()
 *
 * \param[in]    recog
 * \param[in]    index into array of char types
 * \param[out]   pcharstr string representation;
 *                        returns an empty string on error
 * \return  0 if found, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) Extracts a copy of the string from sa_text, which
 *          the caller must free.
 *      (2) Caller must check the function return value.
 * </pre>
 */
l_int32
recogGetClassString(L_RECOG  *recog,
                    l_int32   index,
                    char    **pcharstr)
{
    PROCNAME("recogGetClassString");

    if (!pcharstr)
        return ERROR_INT("&charstr not defined", procName, 1);
    *pcharstr = stringNew("");
    if (!recog)
        return ERROR_INT("recog not defined", procName, 2);

    if (index < 0 || index >= recog->setsize)
        return ERROR_INT("invalid index", procName, 1);
    LEPT_FREE(*pcharstr);
    *pcharstr = sarrayGetString(recog->sa_text, index, L_COPY);
    return 0;
}


/*!
 * \brief   l_convertCharstrToInt()
 *
 * \param[in]    str input string representing one UTF-8 character;
 *                   not more than 4 bytes
 * \param[out]   pval integer value for the input.  Think of it
 *                    as a 1-to-1 hash code.
 * \return  0 if OK, 1 on error
 */
l_int32
l_convertCharstrToInt(const char  *str,
                      l_int32     *pval)
{
l_int32  size, val;

    PROCNAME("l_convertCharstrToInt");

    if (!pval)
        return ERROR_INT("&val not defined", procName, 1);
    *pval = 0;
    if (!str)
        return ERROR_INT("str not defined", procName, 1);
    size = strlen(str);
    if (size == 0)
        return ERROR_INT("empty string", procName, 1);
    if (size > 4)
        return ERROR_INT("invalid string: > 4 bytes", procName, 1);

    val = (l_int32)str[0];
    if (size > 1)
        val = (val << 8) + (l_int32)str[1];
    if (size > 2)
        val = (val << 8) + (l_int32)str[2];
    if (size > 3)
        val = (val << 8) + (l_int32)str[3];
    *pval = val;
    return 0;
}


/*------------------------------------------------------------------------*
 *                             Serialization                              *
 *------------------------------------------------------------------------*/
/*!
 * \brief   recogRead()
 *
 * \param[in]    filename
 * \return  recog, or NULL on error
 *
 * <pre>
 * Notes:
 *      (1) When a recog is serialized, a pixaa of the templates that are
 *          actually used for correlation is saved in the pixaa_u array
 *          of the recog.  These can be different from the templates that
 *          were used to generate the recog, because those original templates
 *          can be scaled and turned into normalized lines.  When recog1
 *          is deserialized to recog2, these templates are put in both the
 *          unscaled array (pixaa_u) and the modified array (pixaa) in recog2.
 *          Why not put it in only the unscaled array and let
 *          recogTrainingFinalized() regenerate the modified templates?
 *          The reason is that with normalized lines, the operation of
 *          thinning to a skeleton and dilating back to a fixed width
 *          is not idempotent.  Thinning to a skeleton saves pixels at
 *          the end of a line segment, and thickening the skeleton puts
 *          additional pixels at the end of the lines.  This tends to
 *          close gaps.
 * </pre>
 */
L_RECOG *
recogRead(const char  *filename)
{
FILE     *fp;
L_RECOG  *recog;

    PROCNAME("recogRead");

    if (!filename)
        return (L_RECOG *)ERROR_PTR("filename not defined", procName, NULL);
    if ((fp = fopenReadStream(filename)) == NULL)
        return (L_RECOG *)ERROR_PTR("stream not opened", procName, NULL);

    if ((recog = recogReadStream(fp)) == NULL) {
        fclose(fp);
        return (L_RECOG *)ERROR_PTR("recog not read", procName, NULL);
    }

    fclose(fp);
    return recog;
}


/*!
 * \brief   recogReadStream()
 *
 * \param[in]    fp file stream
 * \return  recog, or NULL on error
 */
L_RECOG *
recogReadStream(FILE  *fp)
{
l_int32   version, setsize, threshold, scalew, scaleh, linew;
l_int32   maxyshift, nc;
L_DNA    *dna_tochar;
PIXAA    *paa;
L_RECOG  *recog;
SARRAY   *sa_text;

    PROCNAME("recogReadStream");

    if (!fp)
        return (L_RECOG *)ERROR_PTR("stream not defined", procName, NULL);

    if (fscanf(fp, "\nRecog Version %d\n", &version) != 1)
        return (L_RECOG *)ERROR_PTR("not a recog file", procName, NULL);
    if (version != RECOG_VERSION_NUMBER)
        return (L_RECOG *)ERROR_PTR("invalid recog version", procName, NULL);
    if (fscanf(fp, "Size of character set = %d\n", &setsize) != 1)
        return (L_RECOG *)ERROR_PTR("setsize not read", procName, NULL);
    if (fscanf(fp, "Binarization threshold = %d\n", &threshold) != 1)
        return (L_RECOG *)ERROR_PTR("binary thresh not read", procName, NULL);
    if (fscanf(fp, "Maxyshift = %d\n", &maxyshift) != 1)
        return (L_RECOG *)ERROR_PTR("maxyshift not read", procName, NULL);
    if (fscanf(fp, "Scale to width = %d\n", &scalew) != 1)
        return (L_RECOG *)ERROR_PTR("width not read", procName, NULL);
    if (fscanf(fp, "Scale to height = %d\n", &scaleh) != 1)
        return (L_RECOG *)ERROR_PTR("height not read", procName, NULL);
    if (fscanf(fp, "Normalized line width = %d\n", &linew) != 1)
        return (L_RECOG *)ERROR_PTR("line width not read", procName, NULL);
    if ((recog = recogCreate(scalew, scaleh, linew, threshold,
                             maxyshift)) == NULL)
        return (L_RECOG *)ERROR_PTR("recog not made", procName, NULL);

    if (fscanf(fp, "\nLabels for character set:\n") != 0)
        return (L_RECOG *)ERROR_PTR("label intro not read", procName, NULL);
    l_dnaDestroy(&recog->dna_tochar);
    sarrayDestroy(&recog->sa_text);
    if ((dna_tochar = l_dnaReadStream(fp)) == NULL)
        return (L_RECOG *)ERROR_PTR("dna_tochar not read", procName, NULL);
    if ((sa_text = sarrayReadStream(fp)) == NULL)
        return (L_RECOG *)ERROR_PTR("sa_text not read", procName, NULL);
    recog->sa_text = sa_text;
    recog->dna_tochar = dna_tochar;

    if (fscanf(fp, "\nPixaa of all samples in the training set:\n") != 0)
        return (L_RECOG *)ERROR_PTR("pixaa intro not read", procName, NULL);
    if ((paa = pixaaReadStream(fp)) == NULL)
        return (L_RECOG *)ERROR_PTR("pixaa not read", procName, NULL);
    recog->setsize = setsize;
    nc = pixaaGetCount(paa, NULL);
    if (nc != setsize) {
        L_ERROR("(setsize = %d) != (paa count = %d)\n", procName,
                     setsize, nc);
        return NULL;
    }

    recogAddAllSamples(recog, paa, 0);  /* this finishes */
    pixaaDestroy(&paa);
    return recog;
}


/*!
 * \brief   recogReadMem()
 *
 * \param[in]    data  serialization of recog (not ascii)
 * \param[in]    size  of data in bytes
 * \return  recog, or NULL on error
 */
L_RECOG *
recogReadMem(const l_uint8  *data,
             size_t          size)
{
FILE     *fp;
L_RECOG  *recog;

    PROCNAME("recogReadMem");

    if (!data)
        return (L_RECOG *)ERROR_PTR("data not defined", procName, NULL);
    if ((fp = fopenReadFromMemory(data, size)) == NULL)
        return (L_RECOG *)ERROR_PTR("stream not opened", procName, NULL);

    recog = recogReadStream(fp);
    fclose(fp);
    if (!recog) L_ERROR("recog not read\n", procName);
    return recog;
}


/*!
 * \brief   recogWrite()
 *
 * \param[in]    filename
 * \param[in]    recog
 * \return  0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) The pixaa of templates that is written is the modified one
 *          in the pixaa field. It is the pixaa that is actually used
 *          for correlation. This is not the unscaled array of labeled
 *          bitmaps, in pixaa_u, that was used to generate the recog in the
 *          first place.  See the notes in recogRead() for the rationale.
 * </pre>
 */
l_int32
recogWrite(const char  *filename,
           L_RECOG     *recog)
{
FILE  *fp;

    PROCNAME("recogWrite");

    if (!filename)
        return ERROR_INT("filename not defined", procName, 1);
    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);

    if ((fp = fopenWriteStream(filename, "wb")) == NULL)
        return ERROR_INT("stream not opened", procName, 1);
    if (recogWriteStream(fp, recog))
        return ERROR_INT("recog not written to stream", procName, 1);
    fclose(fp);
    return 0;
}


/*!
 * \brief   recogWriteStream()
 *
 * \param[in]    fp file stream opened for "wb"
 * \param[in]    recog
 * \return  0 if OK, 1 on error
 */
l_int32
recogWriteStream(FILE     *fp,
                 L_RECOG  *recog)
{
    PROCNAME("recogWriteStream");

    if (!fp)
        return ERROR_INT("stream not defined", procName, 1);
    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);

    fprintf(fp, "\nRecog Version %d\n", RECOG_VERSION_NUMBER);
    fprintf(fp, "Size of character set = %d\n", recog->setsize);
    fprintf(fp, "Binarization threshold = %d\n", recog->threshold);
    fprintf(fp, "Maxyshift = %d\n", recog->maxyshift);
    fprintf(fp, "Scale to width = %d\n", recog->scalew);
    fprintf(fp, "Scale to height = %d\n", recog->scaleh);
    fprintf(fp, "Normalized line width = %d\n", recog->linew);
    fprintf(fp, "\nLabels for character set:\n");
    l_dnaWriteStream(fp, recog->dna_tochar);
    sarrayWriteStream(fp, recog->sa_text);
    fprintf(fp, "\nPixaa of all samples in the training set:\n");
    pixaaWriteStream(fp, recog->pixaa);

    return 0;
}


/*!
 * \brief   recogWriteMem()
 *
 * \param[out]   pdata data of serialized recog (not ascii)
 * \param[out]   psize size of returned data
 * \param[in]    recog
 * \return  0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) Serializes a recog in memory and puts the result in a buffer.
 * </pre>
 */
l_int32
recogWriteMem(l_uint8  **pdata,
              size_t    *psize,
              L_RECOG   *recog)
{
l_int32  ret;
FILE    *fp;

    PROCNAME("recogWriteMem");

    if (pdata) *pdata = NULL;
    if (psize) *psize = 0;
    if (!pdata)
        return ERROR_INT("&data not defined", procName, 1);
    if (!psize)
        return ERROR_INT("&size not defined", procName, 1);
    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);

#if HAVE_FMEMOPEN
    if ((fp = open_memstream((char **)pdata, psize)) == NULL)
        return ERROR_INT("stream not opened", procName, 1);
    ret = recogWriteStream(fp, recog);
#else
    L_INFO("work-around: writing to a temp file\n", procName);
  #ifdef _WIN32
    if ((fp = fopenWriteWinTempfile()) == NULL)
        return ERROR_INT("tmpfile stream not opened", procName, 1);
  #else
    if ((fp = tmpfile()) == NULL)
        return ERROR_INT("tmpfile stream not opened", procName, 1);
  #endif  /* _WIN32 */
    ret = recogWriteStream(fp, recog);
    rewind(fp);
    *pdata = l_binaryReadStream(fp, psize);
#endif  /* HAVE_FMEMOPEN */
    fclose(fp);
    return ret;
}


/*!
 * \brief   recogExtractPixa()
 *
 * \param[in]   recog
 * \return  pixa if OK, NULL on error
 *
 * <pre>
 * Notes:
 *      (1) This generates a pixa of all the unscaled images in the
 *          recognizer, where each one has its character class label in
 *          the pix text field, by flattening pixaa_u to a pixa.
 * </pre>
 */
PIXA *
recogExtractPixa(L_RECOG  *recog)
{
    PROCNAME("recogExtractPixa");

    if (!recog)
        return (PIXA *)ERROR_PTR("recog not defined", procName, NULL);

    recogAddCharstrLabels(recog);
    return pixaaFlattenToPixa(recog->pixaa_u, NULL, L_CLONE);
}


/*!
 * \brief   recogAddCharstrLabels()
 *
 * \param[in]    recog
 * \return  0 if OK, 1 on error
 */
static l_int32
recogAddCharstrLabels(L_RECOG  *recog)
{
char    *text;
l_int32  i, j, n1, n2;
PIX     *pix;
PIXA    *pixa;
PIXAA   *paa;

    PROCNAME("recogAddCharstrLabels");

    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);

        /* Add the labels to each unscaled pix */
    paa = recog->pixaa_u;
    n1 = pixaaGetCount(paa, NULL);
    for (i = 0; i < n1; i++) {
        pixa = pixaaGetPixa(paa, i, L_CLONE);
        text = sarrayGetString(recog->sa_text, i, L_NOCOPY);
        n2 = pixaGetCount(pixa);
        for (j = 0; j < n2; j++) {
             pix = pixaGetPix(pixa, j, L_CLONE);
             pixSetText(pix, text);
             pixDestroy(&pix);
        }
        pixaDestroy(&pixa);
    }

    return 0;
}


/*!
 * \brief   recogAddAllSamples()
 *
 * \param[in]    recog
 * \param[in]    paa    pixaa from previously trained recog
 * \param[in]    debug
 * \return  0 if OK, 1 on error
 *
 * <pre>
 * Notes:
 *      (1) This is used with the serialization routine recogRead(),
 *          where each pixa in the pixaa represents a set of characters
 *          in a different class.  Before calling this function, we have
 *          verified that the number of character classes, given by the
 *          setsize field in %recog, equals the number of pixa in the paa.
 *          The character labels for each set are in the sa_text field.
 * </pre>
 */
static l_int32
recogAddAllSamples(L_RECOG  *recog,
                   PIXAA    *paa,
                   l_int32   debug)
{
char    *text;
l_int32  i, j, nc, ns;
PIX     *pix;
PIXA    *pixa, *pixa1;

    PROCNAME("recogAddAllSamples");

    if (!recog)
        return ERROR_INT("recog not defined", procName, 1);
    if (!paa)
        return ERROR_INT("paa not defined", procName, 1);

    nc = pixaaGetCount(paa, NULL);
    for (i = 0; i < nc; i++) {
        pixa = pixaaGetPixa(paa, i, L_CLONE);
        ns = pixaGetCount(pixa);
        text = sarrayGetString(recog->sa_text, i, L_NOCOPY);
        pixa1 = pixaCreate(ns);
        pixaaAddPixa(recog->pixaa_u, pixa1, L_INSERT);
        for (j = 0; j < ns; j++) {
            pix = pixaGetPix(pixa, j, L_CLONE);
            if (debug) fprintf(stderr, "pix[%d,%d]: text = %s\n", i, j, text);
            pixaaAddPix(recog->pixaa_u, i, pix, NULL, L_INSERT);
        }
        pixaDestroy(&pixa);
    }

    recogTrainingFinished(recog, 0);  /* 0: see comment in recogRead() */
    return 0;
}
